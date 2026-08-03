#pragma once
#include <memory>
#include "Math/SRMath.h"

class Texture;

struct Material
{
	std::string name;						// ��Ƽ���� �̸�
	SRMath::vec3 ka = { 0.5f, 0.5f, 0.5f};	// �⺻ Ȯ�� ����
	SRMath::vec3 kd = { 0.5f, 0.5f, 0.5f};	// �⺻ ���ݻ� ����
	SRMath::vec3 ks = { 0.5f, 0.5f, 0.5f};	// �⺻ ���ݻ� ����
	float Ns = 32.0f;						// �⺻ �ݻ���
	float d = 1.0f;							// ����� (�⺻��: 1.0, ���� ������)

	std::shared_ptr<Texture> 
		diffuseTexture = nullptr;			// �ؽ���
	int illum = 2;							// ���� �� (�⺻��: 2, Phong ���� ��)
};
