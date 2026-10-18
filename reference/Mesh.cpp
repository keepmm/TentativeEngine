#include "Mesh.hpp"
#include "d3dx12.h"
#include <array>
#include <cstdint>
#include <cstring>
#include "DirectX.hpp"

namespace
{
	constexpr float C = 0.6f;
	constexpr float4 Color = { C, C, C, 1.0f };
}

void Mesh::Init(
	const ComPtr<ID3D12Device>& device,
	const std::vector<Vertex>& vertices,
	const std::vector<std::uint32_t>& indices,
	const std::vector<SubMesh>* subMeshes)
{
	// 引数のどれかが空の場合初期化しない
	if(device == nullptr || vertices.empty() || indices.empty())
	{
		return;
	}

	const UINT vertexBufferSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));
	const UINT indexBufferSize = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));
	m_VertexCount = static_cast<UINT>(vertices.size());

	// 頂点バッファの作成
		CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);

	{
		auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
		device->CreateCommittedResource(
			&uploadHeap,
			D3D12_HEAP_FLAG_NONE,
			&vbDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_VertexBuffer));

		void* vbMapped = nullptr;
		CD3DX12_RANGE readRange(0, 0); // 読み取りはしない
		m_VertexBuffer->Map(0, &readRange, &vbMapped);
		std::memcpy(vbMapped, vertices.data(), vertexBufferSize);
		m_VertexBuffer->Unmap(0, nullptr);

		m_VertexBufferView.BufferLocation = m_VertexBuffer->GetGPUVirtualAddress();
		m_VertexBufferView.StrideInBytes = sizeof(Vertex);
		m_VertexBufferView.SizeInBytes = vertexBufferSize;
	}

	{
		auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);
		device->CreateCommittedResource(
			&uploadHeap,
			D3D12_HEAP_FLAG_NONE,
			&ibDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_IndexBuffer));
		void* ibMapped = nullptr;
		CD3DX12_RANGE readRange(0, 0); // 読み取りはしない
		m_IndexBuffer->Map(0, &readRange, &ibMapped);
		std::memcpy(ibMapped, indices.data(), indexBufferSize);
		m_IndexBuffer->Unmap(0, nullptr);
		m_IndexBufferView.BufferLocation = m_IndexBuffer->GetGPUVirtualAddress();
		m_IndexBufferView.Format = DXGI_FORMAT_R32_UINT;
		m_IndexBufferView.SizeInBytes = indexBufferSize;
	}

	m_IndexCount = static_cast<UINT>(indices.size());

	m_SubMeshes.clear();
	if(subMeshes != nullptr && !subMeshes->empty())
	{
		m_SubMeshes = *subMeshes;
	}
	else
	{
		m_SubMeshes.emplace_back(SubMesh{ 0, m_IndexCount, 0 });
	}
}

void Mesh::CreateCube(const ComPtr<ID3D12Device>& device)
{
	const std::array<Vertex, 24> vertices =
	{
		Vertex{ { -0.5f,  0.5f, -0.5f },{ 0.0f, 0.0f, -1.0f }, Color,{ 0.0f, 0.0f } },
		Vertex{ {  0.5f,  0.5f, -0.5f },{ 0.0f, 0.0f, -1.0f }, Color,{ 1.0f, 0.0f } },
		Vertex{ { -0.5f, -0.5f, -0.5f },{ 0.0f, 0.0f, -1.0f }, Color,{ 0.0f, 1.0f } },
		Vertex{ {  0.5f, -0.5f, -0.5f },{ 0.0f, 0.0f, -1.0f }, Color,{ 1.0f, 1.0f } },

		Vertex{ {  0.5f,  0.5f,  0.5f },{ 0.0f, 0.0f,  1.0f }, Color,{ 0.0f, 0.0f } },
		Vertex{ { -0.5f,  0.5f,  0.5f },{ 0.0f, 0.0f,  1.0f }, Color,{ 1.0f, 0.0f } },
		Vertex{ {  0.5f, -0.5f,  0.5f },{ 0.0f, 0.0f,  1.0f }, Color,{ 0.0f, 1.0f } },
		Vertex{ { -0.5f, -0.5f,  0.5f },{ 0.0f, 0.0f,  1.0f }, Color,{ 1.0f, 1.0f } },

		Vertex{ { -0.5f,  0.5f,  0.5f },{ -1.0f, 0.0f, 0.0f }, Color,{ 0.0f, 0.0f } },
		Vertex{ { -0.5f,  0.5f, -0.5f },{ -1.0f, 0.0f, 0.0f }, Color,{ 1.0f, 0.0f } },
		Vertex{ { -0.5f, -0.5f,  0.5f },{ -1.0f, 0.0f, 0.0f }, Color,{ 0.0f, 1.0f } },
		Vertex{ { -0.5f, -0.5f, -0.5f },{ -1.0f, 0.0f, 0.0f }, Color,{ 1.0f, 1.0f } },

		Vertex{ {  0.5f,  0.5f, -0.5f },{ 1.0f, 0.0f, 0.0f }, Color,{ 0.0f, 0.0f } },
		Vertex{ {  0.5f,  0.5f,  0.5f },{ 1.0f, 0.0f, 0.0f }, Color,{ 1.0f, 0.0f } },
		Vertex{ {  0.5f, -0.5f, -0.5f },{ 1.0f, 0.0f, 0.0f }, Color,{ 0.0f, 1.0f } },
		Vertex{ {  0.5f, -0.5f,  0.5f },{ 1.0f, 0.0f, 0.0f }, Color,{ 1.0f, 1.0f } },

		Vertex{ { -0.5f,  0.5f,  0.5f },{ 0.0f, 1.0f, 0.0f }, Color,{ 0.0f, 0.0f } },
		Vertex{ {  0.5f,  0.5f,  0.5f },{ 0.0f, 1.0f, 0.0f }, Color,{ 1.0f, 0.0f } },
		Vertex{ { -0.5f,  0.5f, -0.5f },{ 0.0f, 1.0f, 0.0f }, Color,{ 0.0f, 1.0f } },
		Vertex{ {  0.5f,  0.5f, -0.5f },{ 0.0f, 1.0f, 0.0f }, Color,{ 1.0f, 1.0f } },

		Vertex{ {  0.5f, -0.5f,  0.5f },{ 0.0f, -1.0f, 0.0f }, Color,{ 0.0f, 0.0f } },
		Vertex{ { -0.5f, -0.5f,  0.5f },{ 0.0f, -1.0f, 0.0f }, Color,{ 1.0f, 0.0f } },
		Vertex{ {  0.5f, -0.5f, -0.5f },{ 0.0f, -1.0f, 0.0f }, Color,{ 0.0f, 1.0f } },
		Vertex{ { -0.5f, -0.5f, -0.5f },{ 0.0f, -1.0f, 0.0f }, Color,{ 1.0f, 1.0f } }
	};

	const std::array<std::uint32_t, 36> indices =
	{
		0,1,2, 1,3,2,
		4,5,6, 5,7,6,
		8,9,10, 9,11,10,
		12,13,14, 13,15,14,
		16,17,18, 17,19,18,
		20,21,22, 21,23,22
	};

	std::vector<Vertex> vb(vertices.begin(), vertices.end());
	std::vector<std::uint32_t> ib(indices.begin(), indices.end());
	Init(device, vb, ib, nullptr);
}

void Mesh::CreateSphere(UINT segments, UINT rings)
{
	segments = (std::max)(3u, segments);
	rings = (std::max)(2u, rings);

	std::vector<Vertex> vertices;
	std::vector<std::uint32_t> indices;
	vertices.reserve(static_cast<size_t>(segments + 1) * (rings + 1));

	for (UINT y = 0; y <= rings; ++y)
	{
		const float v = static_cast<float>(y) / rings;
		const float theta = v * DirectX::XM_PI;
		const float st = sinf(theta);
		const float ct = cosf(theta);

		for (UINT x = 0; x <= segments; ++x)
		{
			const float u = static_cast<float>(x) / segments;
			const float phi = u * DirectX::XM_2PI;
			const float sp = sinf(phi);
			const float cp = cosf(phi);

			Vertex vert{};
			const float3 n = { st * cp,ct,st * sp };
			vert.position = { n.x * 0.5f,n.y * 0.5f,n.z * 0.5f };	// 直径1
			vert.normal = n;
			vert.col = Color;
			vert.uv = { u,v };
			vert.tangent = { -sp,0.0f,cp };
			vertices.push_back(vert);
		}
	}

	for (UINT y = 0; y < rings; ++y)
	{
		for (UINT x = 0; x < segments; ++x)
		{
			const std::uint32_t i0 = y * (segments + 1) + x;
			const std::uint32_t i1 = i0 + 1;
			const std::uint32_t i2 = i0 + (segments + 1);
			const std::uint32_t i3 = i2 + 1;

			// ワインディングは CreateCube と同じ並びに合わせてある
			indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
			indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
		}
	}

	Init(APP->GetDevice(), vertices, indices, nullptr);
}

void Mesh::Draw(ID3D12GraphicsCommandList* commandList) const
{
	if (commandList == nullptr || m_IndexCount == 0) {
		return;
	}

	commandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);
	commandList->IASetIndexBuffer(&m_IndexBufferView);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawIndexedInstanced(m_IndexCount, 1, 0, 0, 0);
}

void Mesh::DrawSubMesh(ID3D12GraphicsCommandList* commandList, UINT subMeshIndex) const
{
	if(commandList == nullptr || subMeshIndex >= m_SubMeshes.size())
	{
		return;
	}

	const auto& sm = m_SubMeshes[subMeshIndex];
	if (sm.indexCount == 0) return;

	commandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);
	commandList->IASetIndexBuffer(&m_IndexBufferView);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawIndexedInstanced(sm.indexCount, 1, sm.indexStart, 0, 0);
}

void Mesh::CreateCylinder(UINT segments)
{
	segments = (std::max)(3u, segments);

	std::vector<Vertex> vertices;
	std::vector<std::uint32_t> indices;

	const float half = 0.5f;
	const float radius = 0.5f;

	// ---- 側面(上下で別頂点。法線が横向きなので共有できない) ---- //
	for (UINT i = 0; i <= segments; ++i)
	{
		const float u = static_cast<float>(i) / segments;
		const float phi = u * DirectX::XM_2PI;
		const float sp = sinf(phi), cp = cosf(phi);
		const float3 n = { cp, 0.0f, sp };

		Vertex top{}, bottom{};
		top.position = { n.x * radius,  half, n.z * radius };
		bottom.position = { n.x * radius, -half, n.z * radius };
		top.normal = bottom.normal = n;
		top.col = bottom.col = Color;
		top.tangent = bottom.tangent = { -sp, 0.0f, cp };
		top.uv = { u, 0.0f };
		bottom.uv = { u, 1.0f };
		vertices.push_back(top);
		vertices.push_back(bottom);
	}

	for (UINT i = 0; i < segments; ++i)
	{
		const std::uint32_t i0 = i * 2;			// 上
		const std::uint32_t i1 = i0 + 1;		// 下
		const std::uint32_t i2 = i0 + 2;		// 次の上
		const std::uint32_t i3 = i0 + 3;		// 次の下

		indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
		indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
	}

	// ---- 上面 / 底面(中心から扇状に張る) ---- //
	auto addCap = [&](float y, const float3& normal, bool flip)
		{
			const std::uint32_t center = static_cast<std::uint32_t>(vertices.size());

			Vertex c{};
			c.position = { 0.0f, y, 0.0f };
			c.normal = normal;
			c.col = Color;
			c.uv = { 0.5f, 0.5f };
			c.tangent = { 1.0f, 0.0f, 0.0f };
			vertices.push_back(c);

			for (UINT i = 0; i <= segments; ++i)
			{
				const float phi = static_cast<float>(i) / segments * DirectX::XM_2PI;
				const float sp = sinf(phi), cp = cosf(phi);

				Vertex v{};
				v.position = { cp * radius, y, sp * radius };
				v.normal = normal;
				v.col = Color;
				v.uv = { cp * 0.5f + 0.5f, sp * 0.5f + 0.5f };
				v.tangent = { 1.0f, 0.0f, 0.0f };
				vertices.push_back(v);
			}

			for (UINT i = 0; i < segments; ++i)
			{
				const std::uint32_t a = center + 1 + i;
				const std::uint32_t b = center + 2 + i;
				if (flip) { indices.push_back(center); indices.push_back(b); indices.push_back(a); }
				else { indices.push_back(center); indices.push_back(a); indices.push_back(b); }
			}
		};

	addCap(half, { 0.0f, 1.0f, 0.0f }, false);
	addCap(-half, { 0.0f, -1.0f, 0.0f }, true);

	Init(APP->GetDevice(), vertices, indices, nullptr);
}

void Mesh::CreateCone(UINT segments)
{
	segments = (std::max)(3u, segments);

	std::vector<Vertex> vertices;
	std::vector<std::uint32_t> indices;

	const float half = 0.5f;
	const float radius = 0.5f;

	// 側面の法線は母線に垂直な向き(斜面に沿う)
	const float slope = radius / (half * 2.0f);
	const float ny = slope / sqrtf(1.0f + slope * slope);
	const float nr = 1.0f / sqrtf(1.0f + slope * slope);

	for (UINT i = 0; i <= segments; ++i)
	{
		const float u = static_cast<float>(i) / segments;
		const float phi = u * DirectX::XM_2PI;
		const float sp = sinf(phi), cp = cosf(phi);

		Vertex apex{}, bottom{};
		apex.position = { 0.0f, half, 0.0f };
		bottom.position = { cp * radius, -half, sp * radius };
		apex.normal = bottom.normal = { cp * nr, ny, sp * nr };
		apex.col = bottom.col = Color;
		apex.tangent = bottom.tangent = { -sp, 0.0f, cp };
		apex.uv = { u, 0.0f };
		bottom.uv = { u, 1.0f };
		vertices.push_back(apex);
		vertices.push_back(bottom);
	}

	for (UINT i = 0; i < segments; ++i)
	{
		const std::uint32_t i0 = i * 2;
		const std::uint32_t i1 = i0 + 1;
		const std::uint32_t i3 = i0 + 3;
		indices.push_back(i0); indices.push_back(i3); indices.push_back(i1);
	}

	// ---- 底面 ---- //
	const std::uint32_t center = static_cast<std::uint32_t>(vertices.size());
	{
		Vertex c{};
		c.position = { 0.0f, -half, 0.0f };
		c.normal = { 0.0f, -1.0f, 0.0f };
		c.col = Color;
		c.uv = { 0.5f, 0.5f };
		c.tangent = { 1.0f, 0.0f, 0.0f };
		vertices.push_back(c);

		for (UINT i = 0; i <= segments; ++i)
		{
			const float phi = static_cast<float>(i) / segments * DirectX::XM_2PI;
			const float sp = sinf(phi), cp = cosf(phi);

			Vertex v{};
			v.position = { cp * radius, -half, sp * radius };
			v.normal = { 0.0f, -1.0f, 0.0f };
			v.col = Color;
			v.uv = { cp * 0.5f + 0.5f, sp * 0.5f + 0.5f };
			v.tangent = { 1.0f, 0.0f, 0.0f };
			vertices.push_back(v);
		}
		for (UINT i = 0; i < segments; ++i)
		{
			indices.push_back(center);
			indices.push_back(center + 2 + i);
			indices.push_back(center + 1 + i);
		}
	}

	Init(APP->GetDevice(), vertices, indices, nullptr);
}

void Mesh::CreateCapsule(UINT segments, UINT rings)
{
	segments = (std::max)(3u, segments);
	rings = (std::max)(2u, rings);

	std::vector<Vertex> vertices;
	std::vector<std::uint32_t> indices;

	const float radius = 0.5f;
	const float half = 0.5f;	// 円柱部分の半分の長さ

	// 上半球 → 円柱 → 下半球 を、1枚の帯としてつなげる
	const UINT totalRings = rings * 2 + 1;
	for (UINT y = 0; y <= totalRings; ++y)
	{
		float yPos = 0.0f;
		float3 n{};

		if (y <= rings)
		{
			// 上半球
			const float t = static_cast<float>(y) / rings;
			const float theta = t * DirectX::XM_PIDIV2;
			n = { sinf(theta), cosf(theta), 0.0f };
			yPos = half + cosf(theta) * radius;
		}
		else
		{
			// 下半球
			const float t = static_cast<float>(y - rings - 1) / rings;
			const float theta = DirectX::XM_PIDIV2 + t * DirectX::XM_PIDIV2;
			n = { sinf(theta), cosf(theta), 0.0f };
			yPos = -half + cosf(theta) * radius;
		}

		for (UINT x = 0; x <= segments; ++x)
		{
			const float u = static_cast<float>(x) / segments;
			const float phi = u * DirectX::XM_2PI;
			const float sp = sinf(phi), cp = cosf(phi);

			Vertex vert{};
			const float3 nn = { n.x * cp, n.y, n.x * sp };
			vert.position = { nn.x * radius, yPos, nn.z * radius };
			vert.normal = nn;
			vert.col = Color;
			vert.uv = { u, static_cast<float>(y) / totalRings };
			vert.tangent = { -sp, 0.0f, cp };
			vertices.push_back(vert);
		}
	}

	for (UINT y = 0; y < totalRings; ++y)
	{
		for (UINT x = 0; x < segments; ++x)
		{
			const std::uint32_t i0 = y * (segments + 1) + x;
			const std::uint32_t i1 = i0 + 1;
			const std::uint32_t i2 = i0 + (segments + 1);
			const std::uint32_t i3 = i2 + 1;

			indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
			indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
		}
	}

	Init(APP->GetDevice(), vertices, indices, nullptr);
}

void Mesh::CreatePlane(UINT divisions)
{
	divisions = (std::max)(1u, divisions);

	std::vector<Vertex> vertices;
	std::vector<std::uint32_t> indices;

	for (UINT z = 0; z <= divisions; ++z)
	{
		for (UINT x = 0; x <= divisions; ++x)
		{
			const float u = static_cast<float>(x) / divisions;
			const float v = static_cast<float>(z) / divisions;

			Vertex vert{};
			vert.position = { u - 0.5f, 0.0f, v - 0.5f };
			vert.normal = { 0.0f, 1.0f, 0.0f };
			vert.col = Color;
			vert.uv = { u, v };
			vert.tangent = { 1.0f, 0.0f, 0.0f };
			vertices.push_back(vert);
		}
	}

	for (UINT z = 0; z < divisions; ++z)
	{
		for (UINT x = 0; x < divisions; ++x)
		{
			const std::uint32_t i0 = z * (divisions + 1) + x;
			const std::uint32_t i1 = i0 + 1;
			const std::uint32_t i2 = i0 + (divisions + 1);
			const std::uint32_t i3 = i2 + 1;

			indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
			indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
		}
	}

	Init(APP->GetDevice(), vertices, indices, nullptr);
}

void Mesh::CreateQuad(const ComPtr<ID3D12Device>& device)
{
	const std::array<Vertex,4> vertices =
	{
		Vertex{ { -0.5f,  0.5f, 0.0f },{ 0.0f, 0.0f, -1.0f }, Color,{ 0.0f, 0.0f } },
		Vertex{ {  0.5f,  0.5f, 0.0f },{ 0.0f, 0.0f, -1.0f }, Color,{ 1.0f, 0.0f } },
		Vertex{ { -0.5f, -0.5f, 0.0f },{ 0.0f, 0.0f, -1.0f }, Color,{ 0.0f, 1.0f } },
		Vertex{ {  0.5f, -0.5f, 0.0f },{ 0.0f, 0.0f, -1.0f }, Color,{ 1.0f, 1.0f } }
	};
	const std::array<std::uint32_t, 6> indices =
	{
		0,1,2,
		1,3,2
	};
	std::vector<Vertex> vb(vertices.begin(), vertices.end());
	std::vector<std::uint32_t> ib(indices.begin(), indices.end());
	Init(device, vb, ib);
}
