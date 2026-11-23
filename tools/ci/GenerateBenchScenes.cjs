const fs = require("node:fs");
const path = require("node:path");
const output = process.argv[2];
fs.mkdirSync(output, {recursive:true});

function createScene(name, countPerSide, halfWidth, halfHeight, detail = 1, overlap = false) {
  const chunks = [];
  let length = 0;
  const document = {
    asset: {version:"2.0", generator:"Renderer scene load measurements"},
    buffers: [], bufferViews: [], accessors: [], meshes: [], nodes: [],
    materials: [{pbrMetallicRoughness:{baseColorFactor:[0.69,0.43,0.23,1], metallicFactor:0, roughnessFactor:0.7}}],
    scenes: [{nodes:[]}], scene: 0
  };
  function accessor(values, isIndex = false) {
    const data = Buffer.alloc(values.length * 4);
    for (let i = 0; i < values.length; ++i) {
      if (isIndex) data.writeUInt32LE(values[i], i * 4);
      else data.writeFloatLE(values[i], i * 4);
    }
    const view = document.bufferViews.push({buffer:0, byteOffset:length, byteLength:data.length}) - 1;
    chunks.push(data);
    length += data.length;
    return document.accessors.push({
      bufferView:view, componentType:isIndex ? 5125 : 5126,
      type:isIndex ? "SCALAR" : "VEC3", count:isIndex ? values.length : values.length / 3
    }) - 1;
  }
  function mesh(positions, indices, min, max) {
    const position = accessor(positions);
    document.accessors[position].min = min;
    document.accessors[position].max = max;
    const normals = [];
    for (let i = 0; i < positions.length / 3; ++i) normals.push(0,0,-1);
    return document.meshes.push({primitives:[{
      attributes:{POSITION:position, NORMAL:accessor(normals)}, indices:accessor(indices,true), material:0
    }]}) - 1;
  }
  function node(spec) {
    document.scenes[0].nodes.push(document.nodes.push(spec) - 1);
  }
  const vertices = [], indices = [];
  for (let y = 0; y <= detail; ++y) {
    for (let x = 0; x <= detail; ++x) vertices.push(x/detail - 0.5,y/detail - 0.5,0);
  }
  for (let y = 0; y < detail; ++y) {
    for (let x = 0; x < detail; ++x) {
      const a = y * (detail + 1) + x;
      const b = a + detail + 1;
      indices.push(a,b+1,a+1,a,b,b+1);
    }
  }
  const tile = mesh(vertices,indices,[-0.5,-0.5,0],[0.5,0.5,0]);
  const anchor = mesh([
    -8,-4.5,0, -7.9999,-4.5,0, -8,-4.4999,0,
    8,4.5,0.6, 7.9999,4.5,0.6, 8,4.4999,0.6
  ], [0,1,2,3,4,5], [-8,-4.5,0], [8,4.5,0.6]);
  node({mesh:anchor,name:"camera bounds anchor"});
  const stepX = 2 * halfWidth / countPerSide;
  const stepY = 2 * halfHeight / countPerSide;
  for (let y = 0; y < countPerSide; ++y) {
    for (let x = 0; x < countPerSide; ++x) {
      const layer = y * countPerSide + x;
      node({mesh:tile,
        translation:overlap ? [0,0,0.1+layer*0.16] : [-halfWidth+(x+0.5)*stepX,-halfHeight+(y+0.5)*stepY,0.1],
        scale:overlap ? [2*halfWidth,2*halfHeight,1] : [stepX,stepY,1]});
    }
  }
  const file = path.join(output,name+".glb");
  document.buffers.push({byteLength:length});
  const json = Buffer.from(JSON.stringify(document));
  const alignedJson = Buffer.alloc((json.length + 3) & ~3,32);
  json.copy(alignedJson);
  const header = Buffer.alloc(12), jsonHeader = Buffer.alloc(8), binHeader = Buffer.alloc(8);
  header.write("glTF");
  header.writeUInt32LE(2,4);
  header.writeUInt32LE(28+alignedJson.length+length,8);
  jsonHeader.writeUInt32LE(alignedJson.length);
  jsonHeader.write("JSON",4);
  binHeader.writeUInt32LE(length);
  binHeader.writeUInt32LE(0x004e4942,4);
  fs.writeFileSync(file,Buffer.concat([header,jsonHeader,alignedJson,binHeader,...chunks]));
  const objects = countPerSide * countPerSide;
  return {name,file,objects,input_triangles:objects * detail * detail * 2,nominal_layers:overlap ? objects : 1};
}

const cases = [
  createScene("few-small",2,2,1.125),
  createScene("many-small",64,2,1.125),
  createScene("few-large",2,7.5,4.1),
  createScene("many-large",64,7.5,4.1),
  createScene("few-dense",2,7.5,4.1,128),
  createScene("overlap",2,7.5,4.1,1,true)
];
fs.writeFileSync(path.join(output,"fixtures.json"),JSON.stringify(cases,null,2));
