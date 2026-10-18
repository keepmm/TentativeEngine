#include "RenderSystem.hpp"
#include "LightSystem.hpp"
#include "../World.hpp"
#include "../Components.hpp"
#include "../FramePipeline.hpp"
#include "../Mesh.hpp"
#include "../Material.hpp"
#include "../DirectX.hpp"
#include "../GpuProfiler.hpp"
#include "../Debug.hpp"
#include "../Animator.hpp"
#include "../d3dx12.h"

#include <algorithm>

#ifdef _FRAMEPIPELINE
const BoneCB& IdentityBoneCB()
{
	static const BoneCB cb = []
		{
			BoneCB c{};
			DirectX::XMFLOAT4X4 id;
			DirectX::XMStoreFloat4x4(&id, DirectX::XMMatrixIdentity());
			for (auto& m : c.boneMatrices) m = id;
			c.morph = 0.0f;
			return c;
		}();
	return cb;
}

void RenderSystem::Publish(_In_ World& world, _In_ FramePipeline& fp)
{
	world.Each<TransformComponent, MeshComponent, MaterialComponent>(
		[&world, &fp](Entity entity,
			TransformComponent& transform,
			MeshComponent& mesh,
			MaterialComponent& material)
		{
			if (mesh.mesh == nullptr || material.material == nullptr)
			{
				return;
			}

			FO_DrawItem item{};
			item.world = transform.world;
			item.mesh = mesh.mesh;
			item.material = material.material;
			item.materials = material.materials;
			item.shaderName = material.shaderName;

			if (world.HasComponent<ReflectionCasterComponent>(entity))
			{
				item.isReflectionCaster = world.GetComponent<ReflectionCasterComponent>(entity).enabled;
			}

			if (world.HasComponent<AnimatorComponent>(entity))
			{
				const auto& an = world.GetComponent<AnimatorComponent>(entity);

				// スキンありのぶんだけフレームメモリを使う(1体 32KB)
				auto* cb = static_cast<BoneCB*>(
					fp.AllocateFrameMemory(sizeof(BoneCB), alignof(BoneCB)));

				const size_t n = (std::min)(an.palette.size(), static_cast<size_t>(MAX_BONES));
				for (size_t i = 0; i < n; ++i) cb->boneMatrices[i] = an.palette[i];
				for (size_t i = n; i < MAX_BONES; ++i)
					DirectX::XMStoreFloat4x4(&cb->boneMatrices[i], DirectX::XMMatrixIdentity());

				bool anyMorph = false;
				for (float w : an.morphWeights)
					if (fabsf(w) > 1e-6f) { anyMorph = true; break; }
				cb->morph = anyMorph ? 1.0f : 0.0f;

				item.boneCb = cb;

				// モーフはフレームメモリへスナップショットする。
				const size_t vcount = mesh.mesh->GetVertexCount();
				if (anyMorph && an.morphoffsets.size() == vcount && vcount > 0)
				{
					const size_t bytes = vcount * sizeof(DirectX::XMFLOAT3);
					auto* dst = static_cast<DirectX::XMFLOAT3*>(
						fp.AllocateFrameMemory(bytes, alignof(DirectX::XMFLOAT3)));
					std::memcpy(dst, an.morphoffsets.data(), bytes);

					item.morphOffsets = dst;
					item.morphVertexCount = static_cast<UINT>(vcount);
				}
			}
			else
			{
				// スキン無しは共有の単位行列を指す(フレームメモリを消費しない)
				item.boneCb = &IdentityBoneCB();
			}

			fp.AddFrameObject<FO_DrawItem>(std::move(item));
		});
}
#endif

void RenderSystem::Draw(
	_In_ World& world,
	_In_ const RenderContext& renderContext,
	_In_ ID3D12PipelineState* overidePso,
	_In_ DrawFilter filter)
{
	if (renderContext.CommandList == nullptr)
	{
		return;
	}

	if (renderContext.useMeshShader)
	{
		if (!renderContext.meshShaderSupported ||
			renderContext.CommandList6 == nullptr ||
			renderContext.meshShaderPso == nullptr)
		{
			return;
		}

		renderContext.CommandList6->SetPipelineState(renderContext.meshShaderPso);
		renderContext.CommandList6->DispatchMesh(1, 1, 1);
	}

	// b2: ライトCB
	if (renderContext.cbAllocator != nullptr)
	{
		const UINT frameSlot = renderContext.frameIndex % RTV_NUM;
		const D3D12_GPU_VIRTUAL_ADDRESS b2 = renderContext.cbAllocator->Allocate(
			frameSlot, &renderContext.lightCb, sizeof(LightCB));
		if (b2 != 0)
		{
			renderContext.CommandList->SetGraphicsRootConstantBufferView(2, b2);
		}
	}

	static ComPtr<ID3D12Resource> s_zeroMorph;
	static D3D12_GPU_VIRTUAL_ADDRESS s_zeroMorphVA = 0;
	if (!s_zeroMorph)
	{
		const UINT ZERO_VERTS = 300000;                  // 最大メッシュ頂点数を余裕でカバー
		const UINT bytes = ZERO_VERTS * sizeof(DirectX::XMFLOAT3);
		CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(bytes);
		APP->GetDevice()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&s_zeroMorph));
		void* p = nullptr; CD3DX12_RANGE rr(0, 0);
		s_zeroMorph->Map(0, &rr, &p);
		memset(p, 0, bytes);
		s_zeroMorph->Unmap(0, nullptr);
		s_zeroMorphVA = s_zeroMorph->GetGPUVirtualAddress();
	}

	auto resolvePass = [this, &renderContext](const std::string& base) -> const std::string&
		{
			if (renderContext.psoSuffix == nullptr || renderContext.psoSuffix[0] == 0)
				return base;
			auto& cache = m_PassCache[renderContext.psoSuffix];
			auto it = cache.find(base);
			if (it == cache.end())
			{
				std::string withSuffix = base + renderContext.psoSuffix;
				it = cache.emplace(base, APP->HasShaderPass(withSuffix) ? withSuffix : base).first;
			}
			return it->second;
		};

	D3D12_GPU_VIRTUAL_ADDRESS identityBoneVA = 0;
	if (renderContext.cbAllocator != nullptr)
	{
		identityBoneVA = renderContext.cbAllocator->Allocate(
			renderContext.frameIndex % RTV_NUM, &IdentityBoneCB(), sizeof(BoneCB));
	}

#ifdef _FRAMEPIPELINE
	FramePipeline* fp = GetFrameThreadPipelineNullable();
	if (fp != nullptr)
	{
		// FramePipeline 経路で Worldを読まずに描画
		fp->ForEachFrameObject<FO_DrawItem>(
			[&](const FO_DrawItem& item)
			{
				if (item.mesh == nullptr || item.material == nullptr)
				{
					return;
				}

				auto passes = [&](const std::string& sn)
					{
						const bool t = APP->IsShaderAlphaBlend(sn);
						const bool toon = IsToonShader(sn);
						switch (filter)
						{
						case DrawFilter::OPAQUEONLY:		return !t;
						case DrawFilter::TRANSPARENTONLY:	return t;
						case DrawFilter::OPAQUE_NOTOON:		return !t && !toon;
						case DrawFilter::OPAQUE_TOON:		return !t && toon;
						default:							return true;
						}
					};

				bool any = false;
				if (!item.materials.empty() && item.mesh->GetSubMeshCount() > 0)
				{
					for (UINT s = 0; s < item.mesh->GetSubMeshCount() && !any; ++s)
					{
						UINT mi = item.mesh->GetSubMeshMaterialIndex(s);
						if (mi >= item.materials.size()) mi = 0;
						const auto& mat = item.materials[mi];
						if (mat) any = passes(mat->shaderName.empty() ? item.shaderName : mat->shaderName);
					}
				}
				else
				{
					any = passes(item.shaderName);
				}

				if (!any)return;

				// 反射パス判定
				if (filter == DrawFilter::REFLECTION && !item.isReflectionCaster)
				{
					return;
				}

				// b4: ボーンCB
				if (renderContext.cbAllocator)
				{
					const UINT slot = renderContext.frameIndex % RTV_NUM;
					D3D12_GPU_VIRTUAL_ADDRESS b4 = identityBoneVA;
					if (item.boneCb)
					{
						b4 = renderContext.cbAllocator->Allocate(slot, item.boneCb, sizeof(BoneCB));
					}
					if (b4) renderContext.CommandList->SetGraphicsRootConstantBufferView(5, b4);

					// t7: モーフ頂点
					D3D12_GPU_VIRTUAL_ADDRESS morphVA = s_zeroMorphVA;
					if (item.morphOffsets && item.morphVertexCount > 0)
					{
						auto va = renderContext.cbAllocator->Allocate(slot, item.morphOffsets, item.morphVertexCount * sizeof(DirectX::XMFLOAT3));
						if (va)morphVA = va;
					}
					renderContext.CommandList->SetGraphicsRootShaderResourceView(6, morphVA);
				}

				const bool multi = !item.materials.empty() && item.mesh->GetSubMeshCount() > 0;

				// 通常描画
				if (multi)
				{
					const UINT sub = item.mesh->GetSubMeshCount();
					for (UINT s = 0; s < sub; ++s)
					{
						UINT mi = item.mesh->GetSubMeshMaterialIndex(s);
						
						if (mi >= item.materials.size()) mi = 0;
						auto& mat = item.materials[mi];
						if (!mat)continue;

						const std::string& sn = mat->shaderName.empty() ? item.shaderName : mat->shaderName;
						const bool isTransparent = APP->IsShaderAlphaBlend(sn);
						const bool isToon = IsToonShader(sn);
						if (filter == DrawFilter::OPAQUEONLY && isTransparent) continue;
						if (filter == DrawFilter::OPAQUE_NOTOON && (isTransparent || isToon)) continue;
						if (filter == DrawFilter::OPAQUE_TOON && (isTransparent || !isToon)) continue;
						if (filter == DrawFilter::TRANSPARENTONLY && !isTransparent) continue;

						mat->Apply(renderContext.CommandList, item.world,
							renderContext.view, renderContext.projection,
							renderContext.wireframe, renderContext.frameIndex,
							renderContext.cbAllocator, resolvePass(sn));
						item.mesh->DrawSubMesh(renderContext.CommandList, s);
					}
				}
				else
				{
					const bool isTransparent = APP->IsShaderAlphaBlend(item.shaderName);
					const bool isToon = IsToonShader(item.shaderName);
					const bool skip = (filter == DrawFilter::OPAQUEONLY && isTransparent) ||
						(filter == DrawFilter::OPAQUE_NOTOON && (isTransparent || isToon)) ||
						(filter == DrawFilter::OPAQUE_TOON && (isTransparent || !isToon)) ||
						(filter == DrawFilter::TRANSPARENTONLY && !isTransparent);

					if (!skip)
					{
						item.material->Apply(renderContext.CommandList, item.world,
							renderContext.view, renderContext.projection,
							renderContext.wireframe, renderContext.frameIndex,
							renderContext.cbAllocator, resolvePass(item.shaderName));
						item.mesh->Draw(renderContext.CommandList);
					}
				}

				// アウトラインパス
				const bool wantsOutline = 
					(item.shaderName == "SkinnedToon" || item.shaderName == "Genshin_Toon")&&
					filter != DrawFilter::OPAQUE_NOTOON &&
					filter != DrawFilter::TRANSPARENTONLY &&
					filter != DrawFilter::REFLECTION;

				if (wantsOutline && !renderContext.wireframe)
				{
					std::string outlineShaderName = resolvePass("Genshin_Outline");
					ID3D12PipelineState* outlinePso = APP->GetPipelineStateByName(outlineShaderName);
					if (outlinePso)
					{
						GPU_PROFILE_SCOPE(renderContext.CommandList, "Draw/Outline");
						if (multi)
						{
							const UINT sub = item.mesh->GetSubMeshCount();
							for (UINT s = 0; s < sub; ++s)
							{
								UINT mi = item.mesh->GetSubMeshMaterialIndex(s);
								if (mi >= item.materials.size()) mi = 0;
								auto& mat = item.materials[mi];
								if (!mat || mat->outlineWidth <= 0.0f) continue;
								const std::string& sn = mat->shaderName.empty() ? item.shaderName : mat->shaderName;
								const bool isTransparent = APP->IsShaderAlphaBlend(sn);
								if (filter == DrawFilter::OPAQUEONLY && isTransparent)		 continue;
								if (filter == DrawFilter::TRANSPARENTONLY && !isTransparent) continue;
								mat->Apply(renderContext.CommandList, item.world,
									renderContext.view, renderContext.projection,
									false, renderContext.frameIndex,
									renderContext.cbAllocator, "", outlinePso);
								item.mesh->DrawSubMesh(renderContext.CommandList, s);
							}
						}
						else if (item.material->outlineWidth > 0.0f)
						{
							item.material->Apply(renderContext.CommandList, item.world,
								renderContext.view, renderContext.projection,
								false, renderContext.frameIndex,
								renderContext.cbAllocator, "", outlinePso);
							item.mesh->Draw(renderContext.CommandList);
						}
					}
				}
			});

			return;
	}
#endif

	world.Each<TransformComponent, MeshComponent, MaterialComponent>(
		[&world, &renderContext, filter, &resolvePass, identityBoneVA](
			Entity entity,
			TransformComponent& transform,
			MeshComponent& mesh,
			MaterialComponent& material
			)
		{
			if (mesh.mesh == nullptr || material.material == nullptr)
			{
				return;
			}

			auto passes = [&](const std::string& sn)
				{
					const bool t = APP->IsShaderAlphaBlend(sn);
					const bool toon = IsToonShader(sn);
					switch (filter)
					{
					case DrawFilter::OPAQUEONLY:      return !t;
					case DrawFilter::TRANSPARENTONLY: return t;
					case DrawFilter::OPAQUE_NOTOON:   return !t && !toon;
					case DrawFilter::OPAQUE_TOON:     return !t && toon;
					default:                          return true;
					}
				};
			bool any = false;
			if (!material.materials.empty() && mesh.mesh->GetSubMeshCount() > 0)
			{
				for (UINT s = 0; s < mesh.mesh->GetSubMeshCount() && !any; ++s)
				{
					UINT mi = mesh.mesh->GetSubMeshMaterialIndex(s);
					if (mi >= material.materials.size()) mi = 0;
					const auto& mat = material.materials[mi];
					if (mat) any = passes(mat->shaderName.empty() ? material.shaderName : mat->shaderName);
				}
			}
			else
			{
				any = passes(material.shaderName);
			}
			if (!any)
			{
				return;
			}

			// 反射パスは ReflectionCaster が付いたEntityだけを描く
			if (filter == DrawFilter::REFLECTION &&
				(!world.HasComponent<ReflectionCasterComponent>(entity) ||
					!world.GetComponent<ReflectionCasterComponent>(entity).enabled))
			{
				return;
			}

			if (renderContext.cbAllocator != nullptr)
			{
				const UINT slot = renderContext.frameIndex % RTV_NUM;
				D3D12_GPU_VIRTUAL_ADDRESS b2 = 0;

				if (world.HasComponent<LightCullComponent>(entity))
				{
					const auto& cull = world.GetComponent<LightCullComponent>(entity);
					LightCB culled{};
					BuildCulledLightCB(renderContext.lightCb,
						transform.position, cull.radius, cull.maxLights, culled);
					b2 = renderContext.cbAllocator->Allocate(slot, &culled, sizeof(LightCB));
				}
				else
				{
					b2 = renderContext.cbAllocator->Allocate(slot,
						&renderContext.lightCb, sizeof(LightCB));
				}

				if (b2 != 0) renderContext.CommandList->SetGraphicsRootConstantBufferView(2, b2);
			}

			// --- b4(root 5): ボーン行列パレット + morphActiveフラグ ---
			if (renderContext.cbAllocator)
			{
				const UINT slot = renderContext.frameIndex % RTV_NUM;
				bool anyMorph = false;
				D3D12_GPU_VIRTUAL_ADDRESS b4 = identityBoneVA;

				if (world.HasComponent<AnimatorComponent>(entity))
				{
					auto& an = world.GetComponent<AnimatorComponent>(entity);
					if (an.palette.size() > MAX_BONES)
					{
						static bool warned = false;
						if (!warned)
						{
							warned = true;
							LOG->LogError("ボーン数が MAX_BONES(" + std::to_string(MAX_BONES)
								+ ") を超えています: " + std::to_string(an.palette.size())
								+ " 超過ぶんは単位行列になりメッシュが破綻します");
						}
					}

					BoneCB cb{};
					const size_t n = (std::min)(an.palette.size(), static_cast<size_t>(MAX_BONES));
					for (size_t i = 0; i < n; ++i) cb.boneMatrices[i] = an.palette[i];
					for (size_t i = n; i < MAX_BONES; ++i)
						DirectX::XMStoreFloat4x4(&cb.boneMatrices[i], DirectX::XMMatrixIdentity());

					for (float w : an.morphWeights)
						if (fabsf(w) > 1e-6f) { anyMorph = true; break; }
					cb.morph = anyMorph ? 1.0f : 0.0f;

					b4 = renderContext.cbAllocator->Allocate(slot, &cb, sizeof(BoneCB));
				}
				if (b4) renderContext.CommandList->SetGraphicsRootConstantBufferView(5, b4);

				// --- t7(root 6): 頂点モーフ ---
				D3D12_GPU_VIRTUAL_ADDRESS morphVA = s_zeroMorphVA;

				if (anyMorph && world.HasComponent<AnimatorComponent>(entity))
				{
					auto& an = world.GetComponent<AnimatorComponent>(entity);
					const size_t vcount = mesh.mesh->GetVertexCount();
					if (an.morphDirty) { RebuildMorphOffsets(an.morphs, an.morphWeights, vcount, an.morphoffsets); an.morphDirty = false; }
					if (an.morphoffsets.size() == vcount)
					{
						if (renderContext.frameSerial != 0 && an.morphVAFrame == renderContext.frameSerial)
						{
							morphVA = an.morphVA;
						}
						else
						{
							auto va = renderContext.cbAllocator->Allocate(slot, an.morphoffsets.data(),
								an.morphoffsets.size() * sizeof(DirectX::XMFLOAT3));
							if (va)
							{
								morphVA = va;
								an.morphVA = va;
								an.morphVAFrame = renderContext.frameSerial;
							}
						}
					}
				}
				renderContext.CommandList->SetGraphicsRootShaderResourceView(6, morphVA);
			}

			const bool multi =
				!material.materials.empty() && mesh.mesh->GetSubMeshCount() > 0;

			// --- 通常描画(全マテリアル・無条件) ---
			if (multi)
			{
				const UINT sub = mesh.mesh->GetSubMeshCount();
				for (UINT s = 0; s < sub; ++s)
				{
					UINT mi = mesh.mesh->GetSubMeshMaterialIndex(s);
					if (mi >= material.materials.size()) mi = 0;
					auto& mat = material.materials[mi];
					if (!mat) continue;

					const std::string& sn = mat->shaderName.empty()
						? material.shaderName : mat->shaderName;

					const bool isTransparent = APP->IsShaderAlphaBlend(sn);
					const bool isToon = IsToonShader(sn);
					if (filter == DrawFilter::OPAQUE_NOTOON && (isTransparent || isToon))  continue;
					if (filter == DrawFilter::OPAQUE_TOON && (isTransparent || !isToon)) continue;
					if (filter == DrawFilter::OPAQUEONLY && isTransparent)		 continue;
					if (filter == DrawFilter::TRANSPARENTONLY && !isTransparent) continue;

					mat->Apply(renderContext.CommandList, transform.world,
						renderContext.view, renderContext.projection,
						renderContext.wireframe, renderContext.frameIndex,
						renderContext.cbAllocator, resolvePass(sn));
					mesh.mesh->DrawSubMesh(renderContext.CommandList, s);
				}
			}
			else
			{
				const bool isTransparent = APP->IsShaderAlphaBlend(material.shaderName);
				const bool isToon = IsToonShader(material.shaderName);
				const bool skip =
					(filter == DrawFilter::OPAQUEONLY && isTransparent) ||
					(filter == DrawFilter::TRANSPARENTONLY && !isTransparent) ||
					(filter == DrawFilter::OPAQUE_NOTOON && (isTransparent || isToon)) ||
					(filter == DrawFilter::OPAQUE_TOON && (isTransparent || !isToon));

				if (!skip)
				{
					material.material->Apply(renderContext.CommandList, transform.world,
						renderContext.view, renderContext.projection,
						renderContext.wireframe, renderContext.frameIndex,
						renderContext.cbAllocator, resolvePass(material.shaderName));
					mesh.mesh->Draw(renderContext.CommandList);
				}
			}

			// --- アウトラインパス(Genshin_Toonのみ・通常描画の後) ---
			const bool wantsOutline =
				(material.shaderName == "Genshin_Toon" || material.shaderName == "SkinnedToon") &&
				filter != DrawFilter::OPAQUE_NOTOON &&
				filter != DrawFilter::TRANSPARENTONLY &&
				filter != DrawFilter::REFLECTION;
			if (wantsOutline && !renderContext.wireframe)
			{
				std::string outlineShaderName = resolvePass("Genshin_Outline");
				ID3D12PipelineState* outlinePso = APP->GetPipelineStateByName(outlineShaderName);
				if (outlinePso)
				{
					GPU_PROFILE_SCOPE(renderContext.CommandList, "Draw/Outline");

					if (multi)
					{
						const UINT sub = mesh.mesh->GetSubMeshCount();
						for (UINT s = 0; s < sub; ++s)
						{
							UINT mi = mesh.mesh->GetSubMeshMaterialIndex(s);
							if (mi >= material.materials.size()) mi = 0;
							auto& mat = material.materials[mi];
							if (!mat) continue;

							if (mat->outlineWidth <= 0.0f) continue;

							const std::string& sn = mat->shaderName.empty()
								? material.shaderName : mat->shaderName;
							const bool isTransparent = APP->IsShaderAlphaBlend(sn);
							if (filter == DrawFilter::OPAQUEONLY && isTransparent)		 continue;
							if (filter == DrawFilter::TRANSPARENTONLY && !isTransparent) continue;

							mat->Apply(renderContext.CommandList, transform.world,
								renderContext.view, renderContext.projection,
								false, renderContext.frameIndex,
								renderContext.cbAllocator, "", outlinePso);
							mesh.mesh->DrawSubMesh(renderContext.CommandList, s);
						}
					}
					else if (material.material->outlineWidth > 0.0f)
					{
						material.material->Apply(renderContext.CommandList, transform.world,
							renderContext.view, renderContext.projection,
							false, renderContext.frameIndex,
							renderContext.cbAllocator, "", outlinePso); 
						mesh.mesh->Draw(renderContext.CommandList);
					}
				}
			}
		}
	);
}
