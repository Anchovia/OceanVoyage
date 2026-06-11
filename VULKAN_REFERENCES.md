# VULKAN_REFERENCES — Vulkan 레퍼런스 운용 노트

> 이 문서는 외부 Vulkan 예제 레포를 OceanVoyage에 어떻게 참고할지 정리한다 — 즉 **"이 기능을 Vulkan으로 어떻게 배선하나"**(API 패턴).
> "**무엇을 구현하나, 누구를 보고 배우나**"(해양 FFT·PBR·TAA·반사·대기 등 그래픽 기법·논문·참고 게임)는 `docs/RENDERING_REFERENCES.md`가 다룬다 — 이 문서와 상호 보완.
> 엔진 구조 결정은 `ARCHITECTURE.md`, 게임 방향은 `DESIGN.md`, 변경 기록은 `DEVLOG.md`에 둔다.
>
> 핵심 원칙: 레퍼런스는 **정답 코드가 아니라 검증된 패턴 사전**이다. 우리 엔진의 `World / GameState / VulkanContext` 책임 분리와 **RTX 3060급 / 1080p~1440p / 60fps / AAA-AA급 사실적 해양 렌더링** 방향을 먼저 유지한다.
> 이전 GTX 1050 Ti~1660 Super 및 스타일라이즈드 Pastel-Farm 기준은 OceanVoyage 렌더링 판단 기준에서 폐기한다.

---

## 주 레퍼런스

- [SaschaWillems/Vulkan](https://github.com/SaschaWillems/Vulkan)
  - Vulkan C++ 예제 모음. triangle부터 texture, shadow, instancing, compute, debug extensions까지 폭이 넓다.
  - 각 기능이 독립 예제로 분리되어 있어 "어떤 Vulkan 기능을 어떻게 배선하는지" 확인하기 좋다.
  - 그대로 이식하기보다, 필요한 샘플의 리소스 수명·descriptor·pipeline·command buffer 패턴을 비교 대상으로 삼는다.
- [KhronosGroup/Vulkan-Samples](https://github.com/KhronosGroup/Vulkan-Samples)
  - SaschaWillems README에서도 공식 샘플 레포로 언급된다.
  - 최신 API 방향, 성능 샘플, 확장 기능의 보조 기준으로 본다.
- [SaschaWillems/HowToVulkan](https://github.com/SaschaWillems/HowToVulkan)
  - SaschaWillems README에서 2026년 Vulkan 입문/이해용으로 연결한 자료.
  - 큰 구조 판단보다 개념 재확인용으로 사용한다.

---

## 우리 엔진에 맞는 참고 방식

- **기능 단위로만 참고한다.** 예제 전체 구조를 따라가면 OceanVoyage의 월드/게임/렌더 경계가 흐려질 수 있다.
- **작은 래퍼부터 흡수한다.** `GpuBuffer`, `PipelineConfig`, `TextureResource`처럼 이미 생긴 국소 추상화를 먼저 키운다. `FrameRenderData`는 현재 농장 상태를 많이 포함하므로, 확장보다 정리가 먼저다.
- **rule of 3 전에는 큰 시스템을 만들지 않는다.** Texture helper, descriptor helper, render pass abstraction은 반복 지점이 실제로 쌓인 뒤 도입한다.
- **품질과 성능을 같이 본다.** 고급 기능은 무조건 배제하지 않는다. RTX 3060급 60fps 예산 안에서 실제 상용 게임에서 쓰이는 안정적이고 확장 가능한 기법을 우선 도입한다.
- **문서화 후 구현한다.** 외부 레퍼런스를 근거로 구조를 바꾸려면 먼저 `ARCHITECTURE.md` 또는 이 문서에 판단을 남긴다.

---

## 우선 참고 샘플 맵

| 주제 | SaschaWillems 샘플 | OceanVoyage 적용 판단 |
|------|--------------------|------------------------|
| Texture upload | [`examples/texture`](https://github.com/SaschaWillems/Vulkan/tree/master/examples/texture) | grass/terrain/object texture mapping의 1순위 참고. staging buffer, image layout transition, sampler, descriptor update 패턴 비교 |
| Texture array | [`examples/texturearray`](https://github.com/SaschaWillems/Vulkan/tree/master/examples/texturearray) | terrain texture mapping 3a에 적용. `sampler2DArray` view, array layer copy, descriptor update 패턴의 비교 기준 |
| Mipmap generation | [`examples/texturemipmapgen`](https://github.com/SaschaWillems/Vulkan/tree/master/examples/texturemipmapgen) | ✅ **도입 완료.** 일반 텍스처 + `sampler2DArray` 밉 생성 + trilinear + 이방성 필터링(디바이스 feature 확인 후) 적용됨. 추가 텍스처 도입 시 같은 경로 재사용 |
| Instancing | [`examples/instancing`](https://github.com/SaschaWillems/Vulkan/tree/master/examples/instancing) | 현재 object/grass instancing과 직접 관련. `ObjectInstance` 확장, variant index, tint, wind phase를 넣을 때 비교 |
| Indirect draw | [`examples/indirectdraw`](https://github.com/SaschaWillems/Vulkan/tree/master/examples/indirectdraw) | 식생/오브젝트 종류와 draw 수가 크게 늘어난 뒤 후보. 지금은 청크별 직접 draw가 단순하고 충분함 |
| Alpha to coverage | [`examples/alphatocoverage`](https://github.com/SaschaWillems/Vulkan/tree/master/examples/alphatocoverage) | grass alpha card 품질 개선 후보. FXAA/SMAA 이후에도 alpha edge가 거칠면 MSAA 정책과 함께 검토 |
| Multisampling | [`examples/multisampling`](https://github.com/SaschaWillems/Vulkan/tree/master/examples/multisampling) | AA 품질 옵션 확장 시 참고. FXAA/SMAA는 post AA로 적용 완료. MSAA/alpha-to-coverage는 alpha edge 품질이 부족할 때 별도 검토 |
| Shadow mapping | [`examples/shadowmapping`](https://github.com/SaschaWillems/Vulkan/tree/master/examples/shadowmapping) | ✅ texel snapping + slope-scaled bias + Poisson PCF 적용됨. 단일맵 한계는 아래 CSM으로 해소 |
| Cascaded shadow mapping | [`examples/shadowmappingcascade`](https://github.com/SaschaWillems/Vulkan/tree/master/examples/shadowmappingcascade) | ✅ **도입 완료.** 3 캐스케이드(바운딩 스피어 fit + 텍셀 스냅 + 캐스케이드 블렌드 밴드 + Poisson PCF). 이 샘플의 split/stabilization 패턴 기반 |
| Offscreen rendering | [`examples/offscreen`](https://github.com/SaschaWillems/Vulkan/tree/master/examples/offscreen) | 현재 post pass와 같은 계열. 추가 후처리, 미니맵, 반사 같은 기능 전 참고 |
| Pipeline statistics | [`examples/pipelinestatistics`](https://github.com/SaschaWillems/Vulkan/tree/master/examples/pipelinestatistics) | DevUI GPU timestamp 다음 단계 후보. 병목이 vertex/fragment 중 어디인지 볼 때 유용 |
| Debug utils | [`examples/debugutils`](https://github.com/SaschaWillems/Vulkan/tree/master/examples/debugutils) | RenderDoc/validation 디버깅 개선 후보. `PASTEL_DEV_BUILD` 전용 객체 이름/구간 라벨 추가에 적합 |
| ImGui overlay | [`examples/imgui`](https://github.com/SaschaWillems/Vulkan/tree/master/examples/imgui) | DevUI는 이미 구현됨. docking/texture preview/graph 확장 때 비교 |

---

## 바로 흡수하기 좋은 패턴

### 1. Texture resource helper

**도입 완료.** grass alpha 텍스처와 SMAA `AreaTex/SearchTex` LUT가 staging upload → image/transition/copy/view 시퀀스를 중복으로 갖던 것을, `GpuBuffer`식 move-only RAII 구조체 `TextureResource` + `createTexture(width, height, format, bytes, size, withSampler)` 헬퍼로 통합했다. sampler는 옵션(grass=자체 sampler, SMAA LUT=공유 `m_postSampler`). Authored texture loading 4a에서는 `stb_image` RGBA8 로더와 `createTextureFromFile(path, withSampler)`를 추가해 같은 업로드 경로를 파일 기반 텍스처에도 재사용한다.

다음 확장 후보:

- terrain/object authored albedo texture, 또는 atlas/array variant 추가
- flower/ground patch 등 dressing texture가 2개 이상 추가
- UI font/atlas texture가 별도 리소스로 들어옴

terrain texture mapping 3a에서는 `sampler2DArray`용 `createTextureArray(width, height, layerCount, format, bytes, size, withSampler)`를 추가해 같은 `TextureResource` 수명 모델을 유지했다. 4a에서는 `assets/textures/grass.png` 선택적 로드 + 절차 fallback까지 연결했다. 4b에서는 `assets/textures/terrain/*.png`로 terrain layer별 Color texture override를 적용했고, 누락되거나 크기가 맞지 않는 layer는 절차 fallback을 유지한다. 4c에서는 `fragColor * rawTexture` 대신 luma/chroma 기반 `materialDetail`을 사용해 texture를 주 색상이 아니라 표면 질감으로 안정화했다. 다음 확장은 material-lite, layer별 strength, mipmap/sampler 옵션이 실제로 필요해지는 시점에 한다.

### 2. Instance data 확장

현재 `ObjectInstance`는 `pos / scale / rot`만 가진다. Vegetation Step 6 1차는 이 포맷을 유지했고, 이후 다음 값이 필요해질 수 있다.

- `tint` 또는 packed color: clump별 초록색 변주
- `variant`: texture array layer 또는 card mesh variant 선택
- `windPhase`: vertex shader sway offset
- `densityClass`: 디버그/튜닝용 분류

Step 6 1차에서는 CPU 배치 규칙만 바꿨다. 색/variant가 실제로 필요해지는 순간 vertex input과 shader를 함께 확장한다.

### 3. Alpha card 품질 개선

현재 grass는 alpha test(`discard`) + depth write로 단순하고 저렴하게 처리한다. OceanVoyage의 현재 렌더링 기준은 RTX 3060급이며, 품질 문제는 저사양 회피가 아니라 안정적인 상용 기법으로 해결한다. 카드 가장자리가 거칠거나 원거리에서 깜빡이면 다음 순서로 본다.

1. texture 자체의 alpha mask 개선
2. mipmap 또는 alpha cutoff 조정
3. FXAA/SMAA 후처리와 조합
4. MSAA가 들어간 뒤 alpha-to-coverage 검토

alpha blend/OIT는 풀밭에는 비용과 정렬 문제가 커서 우선순위가 낮다.

### 4. Dev 디버깅 개선

SaschaWillems의 debug utils/pipeline statistics 계열은 DevUI와 궁합이 좋다. 다음 기능은 `PASTEL_DEV_BUILD`에만 넣는 후보로 둔다.

- Vulkan object name 라벨
- command buffer debug label
- draw call/instance count 표시
- vertex shader vs fragment shader invocation 통계
- grass density heat/debug overlay

---

## 장기 후보

- **Indirect draw**: vegetation/object draw call이 많아지고 CPU command recording이 병목으로 확인될 때.
- **Compute cull/LOD**: 고정맵 + 대량 식생 + 거리 LOD가 실제 병목이 된 뒤.
- **Dynamic rendering**: render pass/framebuffer 구조 변경 필요성이 여러 번 생긴 뒤. 지금은 기존 render pass 구조 유지.
- **Timeline semaphore**: asset streaming 또는 async upload가 실제로 들어갈 때.
- **Descriptor indexing / descriptor heap 계열**: texture/material 수가 크게 늘어난 뒤. 현재는 단순 descriptor set이 더 읽기 쉽다.
- **glTF loading**: authored asset pipeline을 시작할 때. 지금의 절차/로우폴리 primitive mesh 단계에서는 보류.

---

## 다음 구현 순서 메모

FXAA와 SMAA 1x 1차 적용은 완료됐다. 현재 상태:

1. Settings의 `AA OFF / FXAA / SMAA` 중 `FXAA`는 실제 post AA 경로에 연결됐다.
2. `AA OFF`는 기존 post grading만 수행한다.
3. `AA SMAA`는 Iryoku SMAA 구조를 참고한 1x 3-pass 경로로 연결됐다.
4. 자체 게임 UI는 post AA 이후 스왑체인에 직접 그려 픽셀 폰트 깨짐을 피한다.

SaschaWillems/Vulkan에는 README 기준 SMAA 샘플이 없고, 참고 가능한 AA 샘플은 MSAA/alpha-to-coverage 계열이다. 정식 SMAA는 Iryoku SMAA의 `AreaTex/SearchTex` LUT와 `edge detection → blend weight → neighborhood blending` 구조를 기준으로 삼는다.

현재 SMAA는 원본 `SMAA_PRESET_ULTRA` 계열 설정을 따른다(`threshold=0.05`, `search=32`, `diag=16`, corner rounding 25). edge detection은 `smaa_edge.frag`에서 HDR scene color를 ACES 톤매핑한 뒤 감마 luma로 변환해 수행한다. 즉 과거의 "HDR-linear edge 검출" 문제는 edge pass 기준으로는 해소됐다. 다만 neighborhood pass는 아직 HDR scene color를 섞은 뒤 마지막에 톤매핑/그레이딩하므로, 완전한 표준 순서인 "tone-map/grade target → SMAA" 구조는 아니다. reprojection/T2x/S2x는 제외.

1. ✅ diagonal detection 포팅 — 완료
2. ✅ Ultra 계열 threshold/search/diag 튜닝 — 완료
3. T2x/S2x 또는 MSAA/alpha-to-coverage 연계
4. 별도 tone-map/grade target을 만든 뒤 SMAA neighborhood까지 perceptual LDR에서 수행하도록 순서 전환(`docs/ENGINE_TODO.md` P1).

권장 순서:

1. ✅ TextureResource helper — 완료
2. ✅ terrain texture mapping 3a — `sampler2DArray` + 청크 UV/layer + vertex color tint 유지 완료
3. ✅ terrain texture art 3b — 절차 64×64 material mask 1차 튜닝 완료. (물은 이후 전용 FFT water pass로 대체됨)
4. ✅ object texture mapping 3c — object vertex UV/layer 배선, WOOD/LEAVES/STONE layer 재사용 완료
5. ✅ authored texture loading 4a — `stb_image` RGBA8 로더 + `assets/textures` 복사 + `grass.png` 선택적 fallback 완료
6. ✅ authored terrain texture override 4b — terrain layer별 Color texture 파일 override + 절차 fallback 완료
7. ✅ texture tone 4c — luma/chroma 기반 `materialDetail`로 raw texture 곱셈 안정화 완료
8. ✅ layer별 texture strength 4d — grass/leaves는 낮게, dirt/farmland/stone은 높게, wood/wheat는 중간값으로 분리 완료
9. ✅ high-quality grass 5a — 3-card clump + density/scale 상향 완료
10. grass wind/LOD/variant
11. material-lite/mipmap/sampler — roughness/specular 상수와 sampler 정책
12. SMAA: diagonal + Ultra + perceptual edge(밤 AA)는 완료. 남은 과제는 T2x/S2x·MSAA/alpha-to-coverage·tone-map/grade target 뒤 SMAA 구조 전환

선박 PBR(Cook-Torrance GGX) 머티리얼은 이미 도입됐다. render graph, bindless, 대형 material system은 실제 필요가 확인될 때 검토한다.

---

## 지금은 참고만 할 영역

- Full IBL / 환경 프로브 중심 샘플: 선박은 이미 PBR(Cook-Torrance GGX) 머티리얼을 쓰지만, 환경 IBL/프로브 전면 도입은 이후 단계. 재질 수요가 분명해지면 단계적으로 참고한다.
- Deferred rendering / SSAO: 현재 조명 수와 스타일에는 forward + baked AO + shadow가 더 단순하다.
- Ray tracing / mesh shader / bindless 대규모 시스템: 현재 목표와 범위에서는 ROI가 낮다.
- Full render graph: `ARCHITECTURE.md`의 anti-goal과 충돌한다. 필요해도 경량 `IRenderPass` 정도부터 검토한다.

---

## Vegetation Step 6에 대한 적용

`density field + variant 기반 grass dressing`은 Vulkan API 자체보다 **인스턴스 생성 규칙과 인스턴스 데이터 설계**가 핵심이다. Step 6 1차에서는 인스턴스 포맷 변경 없이 density field와 scale/offset variation만 적용했다.

적용 결과:

1. `buildGrassDressingBuffer` 안에 좌표 기반 density field를 추가했다.
2. 균등 확률 대신 patch 단위 밀도와 open grass bias를 사용한다.
3. `ObjectInstance` 포맷 변경 없이 scale/offset 범위를 density에 따라 다르게 준다.
4. 5a에서는 인스턴스 포맷을 유지한 채 3-card clump와 density/scale 값을 올렸다. 다음은 wind sway, 거리 LOD/fade, patch 대비를 우선 본다.
5. 색/texture/card variant는 실제 필요가 확인될 때 인스턴스 포맷과 `grass.vert/.frag`를 함께 확장한다.
6. grass 품질 문제가 alpha edge에서 확인되면 `alphatocoverage`, `multisampling`, `texturemipmapgen` 샘플을 다시 본다.

이렇게 하면 Step 6는 수술적으로 작게 시작하면서도, 이후 texture array / wind / LOD / indirect draw로 확장할 길을 막지 않는다.

---

## Vegetation Step 7에 대한 메모

`ground dressing layer` 1차는 Vulkan 기능 자체보다 **식생과 별도의 placement layer를 청크 단위로 만들 수 있는지**를 확인하는 작업이었다. 현재 구현은 별도 ground patch/pebble instance buffer를 만들고 object pipeline으로 그리는 구조라, 나중에 texture/card/decal 계열 표현으로 교체해도 배치 규칙을 재사용할 수 있다.

검증 결과와 판단:

1. 구조적으로는 visual-only dressing buffer, draw order, object 회피, grass/dirt/open-sky 조건을 검증했다.
2. 미학적으로는 실패에 가깝다. 갈색 patch와 pebble 임시 표현이 너무 크고 대비가 강해 지면 디테일이 아니라 화면을 어지럽히는 오브젝트처럼 보였다.
3. 최종 목표는 geometry patch 대량 배치가 아니라 texture/alpha detail, 작은 color breakup, 낮은 대비의 ground dressing이다.
4. cleanup에서는 Step 7 구조를 유지하되 patch 밀도/크기/대비를 크게 낮추고, grass 위의 ground patch는 사실상 제거했다. 결과가 거의 안 보이긴 하지만, 최종 texture/card detail 전 기준 화면으로는 과한 임시 표현보다 낫다.
5. 다음 작업에서는 grass texture/card detail과 wind를 우선 검토하고, ground detail은 texture/card/decal 계열로 바꿀 때 다시 늘린다.
6. texture 리소스가 grass 외 2개 이상으로 늘어나는 시점에 SaschaWillems `texture`, `texturearray`, `texturemipmapgen` 샘플을 다시 보고 `TextureResource` helper 추출을 검토한다.

---

## Vegetation Step 8에 대한 메모

`grass tint/card variation` 1차는 인스턴스 포맷 확장 없이 shader hash로 처리했다. `ObjectInstance`에 tint/variant 필드를 추가하면 object/grass/shadow 계열 vertex input을 함께 재검토해야 하므로, 실제 texture array나 per-instance material이 필요해질 때까지 미룬다.

적용 결과:

1. `grass.vert`에서 instance position 기반 hash로 clump별 폭/높이 variation을 계산했다.
2. 같은 hash 계열로 tint를 만들어 `grass.frag`에서 grass texture 색에 곱한다.
3. C++ pipeline, descriptor, instance buffer, placement rule은 변경하지 않았다.
4. 유저 빌드 검증 결과: 정상 실행. 반복감은 줄었고 화면은 과하게 지저분해지지 않았다.

---

## 문서 갱신 규칙

- 새 Vulkan 레퍼런스 판단은 우선 이 문서에 적는다.
- 구조 결정으로 굳으면 `ARCHITECTURE.md`에 짧게 승격한다.
- 실제 구현 완료와 검증은 `DEVLOG.md`에 남긴다.
- 게임 경험/비주얼 방향 자체가 바뀔 때만 `DESIGN.md`를 수정한다.
