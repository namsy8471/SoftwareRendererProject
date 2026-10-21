#include "IO/BackgroundLoader.h"
#include "IO/FileIO.h"
#include "IO/Json.h"
#include "Graphics/Model.h"
#include "Graphics/ModelLoader.h"

#include <atomic>
#include <chrono>
#include <expected>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "TestHarness.h"
#include "TestSupport.h"

using namespace sr::test;

namespace
{
using sr::json::Value;

void jsonParseTests()
{
    const auto parsed = sr::json::Parse(
        "\xEF\xBB\xBF { \"name\": \"scene\", \"count\": 3, \"scale\": -1.5e-2, \"on\": true,"
        " \"off\": false, \"none\": null, \"list\": [1, [2, {}], []] }");
    check(parsed.has_value(), "parse object");
    check(*parsed->Find("name")->AsString() == "scene", "string member");
    check(*parsed->Find("count")->AsNumber() == 3.0, "integer member");
    check(*parsed->Find("scale")->AsNumber() == -1.5e-2, "exponent member");
    check(*parsed->Find("on")->AsBool() && !*parsed->Find("off")->AsBool(), "bool members");
    check(parsed->Find("none")->IsNull() && !parsed->Find("missing"), "null and missing");
    const auto &list = *parsed->Find("list")->AsArray();
    check(list.size() == 3 && list[1].AsArray()->size() == 2 && list[2].AsArray()->empty(), "nested arrays");
    const auto &object = *parsed->AsObject();
    check(object.front().key == "name" && object.back().key == "list", "key order kept");

    // 이스케이프: 서로게이트 쌍(😀 U+1F600), 2/3바이트 UTF-8 원문, 제어 문자 이스케이프.
    const auto text = sr::json::Parse(R"("a\"b\\c\/\n\t\u00e9\u20ac\ud83d\ude00 한")");
    check(text && *text->AsString() == "a\"b\\c/\n\t\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80 \xED\x95\x9C",
          "string escapes");
}

void jsonErrorTests()
{
    const auto errorAt = [](std::string_view input, std::size_t line, std::size_t column)
    {
        const auto result = sr::json::Parse(input);
        check(!result.has_value(), "invalid JSON rejected");
        check(result.error().line == line && result.error().column == column,
              "error position: " + result.error().message);
    };
    errorAt("{\n  \"a\": 1,\n  \"a\": 2\n}", 3, 3); // 중복 키
    errorAt("[1, 2,]", 1, 7);                     // 끝 쉼표
    errorAt("01", 1, 2);                           // 앞자리 0 뒤 숫자는 trailing
    errorAt("+1", 1, 1);
    errorAt("1.", 1, 3);
    errorAt("\"abc", 1, 5);
    errorAt("\"a\tb\"", 1, 3);       // 문자열 안 제어 문자
    errorAt("\"\\ud83d\"", 1, 2);    // 짝 없는 서로게이트
    errorAt("\"\xC3\x28\"", 1, 2);   // 잘못된 UTF-8
    errorAt("\"\xED\xA0\x80\"", 1, 2); // UTF-8로 인코딩된 서로게이트
    errorAt("tru", 1, 1);
    errorAt("{} x", 1, 4);
    errorAt("", 1, 1);
    errorAt("1e999", 1, 1);

    std::string deep(100, '[');
    deep.append(100, ']');
    check(!sr::json::Parse(deep).has_value(), "depth limit");
    check(sr::json::Parse(deep, {.maxDepth = 128}).has_value(), "depth limit option");
}

void jsonWriteTests()
{
    Value root;
    root.Set("name", "Main");
    root.Set("version", 1);
    root.Set("ratio", 0.1);
    root.Set("tiny", 1e-300);
    root.Set("text", "line\n\"quote\"\x01");
    root.Set("items", Value::Array{Value(true), Value(nullptr), Value(Value::Object{})});
    root.Set("version", 2); // 같은 키는 자리를 지키며 값만 바뀐다

    const auto compact = sr::json::Write(root, {.indent = 0});
    check(compact.has_value(), "write");
    check(*compact == "{\"name\":\"Main\",\"version\":2,\"ratio\":0.1,\"tiny\":1e-300,"
                      "\"text\":\"line\\n\\\"quote\\\"\\u0001\",\"items\":[true,null,{}]}",
          "compact text: " + *compact);

    const auto pretty = sr::json::Write(root);
    check(pretty && pretty->find("\n  \"name\": \"Main\"") != std::string::npos, "indented text");
    const auto reparsed = sr::json::Parse(*pretty);
    check(reparsed && *reparsed == root, "round trip keeps every value");

    // 최단 표현으로 써도 double 비트가 그대로 돌아와야 한다.
    for (const double number : {0.1, 1.0 / 3.0, 123456789.125, -2.5e-8, 1.7976931348623157e308})
    {
        const auto written = sr::json::Write(Value(number));
        const auto back = sr::json::Parse(*written);
        check(back && *back->AsNumber() == number, "number round trip");
    }
    check(!sr::json::Write(Value(std::numeric_limits<double>::quiet_NaN())).has_value(), "NaN rejected");
    check(!sr::json::Write(Value(std::numeric_limits<double>::infinity())).has_value(), "infinity rejected");
}

void fileTests(const TestContext &context)
{
    const auto directory = context.outputDirectory / "io";
    const auto path = directory / "nested/file.json";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);

    const std::string first = "{\"a\":1}";
    check(sr::io::WriteFileAtomically(path, first).has_value(), "atomic write creates folders");
    check(sr::io::ReadWholeFile(path).value() == first, "read back");
    const std::string second = "{\"a\":2,\"b\":[]}";
    check(sr::io::WriteFileAtomically(path, second).has_value(), "atomic overwrite");
    check(sr::io::ReadWholeFile(path).value() == second, "overwrite replaced content");
    auto temporary = path;
    temporary += ".tmp";
    check(!std::filesystem::exists(temporary), "temporary file removed");

    check(!sr::io::ReadWholeFile(directory / "absent.bin").has_value(), "missing file");
    const auto tooLarge = sr::io::ReadWholeFile(path, 4);
    check(!tooLarge && tooLarge.error().message == "File is too large", "size limit");
}

void backgroundLoaderTests(const TestContext &context)
{
    using namespace std::chrono_literals;
    const auto waitFor = [](sr::io::BackgroundLoader &loader, std::size_t count)
    {
        std::size_t applied = 0;
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (applied < count && std::chrono::steady_clock::now() < deadline)
        {
            applied += loader.DrainCompleted();
            std::this_thread::sleep_for(1ms);
        }
        return applied;
    };

    // work는 작업 스레드, apply는 DrainCompleted를 부른 스레드에서 돈다. 결과는 제출 순서로 끝난다.
    std::atomic<int> wakeups = 0;
    sr::io::BackgroundLoader loader({.onCompleted = [&] { ++wakeups; }});
    const auto mainThread = std::this_thread::get_id();
    std::vector<int> applied;
    bool workOffMainThread = true;
    bool applyOnMainThread = true;
    for (int index = 0; index < 4; ++index)
        loader.Submit(
            [&, index](std::stop_token)
            {
                workOffMainThread = workOffMainThread && std::this_thread::get_id() != mainThread;
                return std::make_unique<int>(index); // 이동 전용 결과
            },
            [&](std::unique_ptr<int> value)
            {
                applyOnMainThread = applyOnMainThread && std::this_thread::get_id() == mainThread;
                applied.push_back(*value);
            });
    check(waitFor(loader, 4) == 4, "all jobs applied");
    check(applied == std::vector<int>{0, 1, 2, 3}, "single worker keeps submit order");
    check(workOffMainThread && applyOnMainThread, "thread contract");
    check(wakeups == 4 && loader.PendingCount() == 0, "completion callback and pending count");

    // 예산 0이어도 한 번에 하나는 실행해 진행을 보장한다.
    std::atomic<int> finished = 0;
    for (int index = 0; index < 3; ++index)
        loader.Submit([&](std::stop_token) { return ++finished; }, [](int) {});
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (finished < 3 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(5ms); // 마지막 결과가 완료 큐에 들어갈 시간
    check(loader.DrainCompleted(0us) == 1, "zero budget runs exactly one");
    check(loader.DrainCompleted() == 2 && loader.PendingCount() == 0, "rest drained later");

    // 실제 OBJ 로드: ModelLoader 내부 parallel_for가 동시성 1 arena에서도 끝나야 한다.
    std::unique_ptr<Model> model;
    loader.Submit([path = context.root / "assets/teapot.obj"](std::stop_token) { return ModelLoader::LoadOBJ(path); },
                  [&](std::expected<std::unique_ptr<Model>, AssetLoadError> result)
                  {
                      check(result.has_value(), "background OBJ load");
                      model = std::move(*result);
                  });
    check(waitFor(loader, 1) == 1 && model && !model->GetMeshes().empty(), "OBJ applied on frame thread");
}

void backgroundLoaderErrorTests()
{
    using namespace std::chrono_literals;
    std::string reported;
    {
        sr::io::BackgroundLoader loader({.onError = [&](const std::string &message) { reported = message; }});
        bool applied = false;
        loader.Submit([](std::stop_token) -> int { throw std::runtime_error("decode failed"); },
                      [&](int) { applied = true; });
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (reported.empty() && std::chrono::steady_clock::now() < deadline)
        {
            loader.DrainCompleted();
            std::this_thread::sleep_for(1ms);
        }
        check(reported == "decode failed" && !applied, "exception reported on drain thread");
    }

    // 소멸: 실행 중인 작업은 stop_token을 보고 끝나고, 시작 안 한 작업과 결과는 버려진다.
    std::atomic<bool> started = false, sawStop = false;
    std::atomic<int> appliedCount = 0;
    {
        sr::io::BackgroundLoader loader;
        loader.Submit(
            [&](std::stop_token stop)
            {
                started = true;
                while (!stop.stop_requested())
                    std::this_thread::sleep_for(1ms);
                sawStop = true;
                return 0;
            },
            [&](int) { ++appliedCount; });
        for (int index = 0; index < 8; ++index)
            loader.Submit([](std::stop_token) { return 0; }, [&](int) { ++appliedCount; });
        while (!started)
            std::this_thread::sleep_for(1ms);
    }
    check(sawStop && appliedCount == 0, "shutdown cancels and discards");
}
} // namespace

namespace sr::test
{
void RegisterIOTests(TestRegistry &registry)
{
    registry.Add("io.json_parse", jsonParseTests);
    registry.Add("io.json_errors", jsonErrorTests);
    registry.Add("io.json_write", jsonWriteTests);
    registry.Add("io.files", fileTests);
    registry.Add("io.background_loader", backgroundLoaderTests);
    registry.Add("io.background_loader_errors", backgroundLoaderErrorTests);
}
} // namespace sr::test
