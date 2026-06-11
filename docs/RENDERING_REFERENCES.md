# RENDERING_REFERENCES — AAA 그래픽 기법·논문·라이브러리·게임 레퍼런스

이 문서는 OceanVoyage의 **AAA/AA급 사실적 해양 렌더링**을 구현하는 데 도움이 되는 **기법·알고리즘·논문·엔진 문서·오픈소스 라이브러리·참고 게임**을 모은다.

- "Vulkan을 *어떻게 배선하는가*"(API 패턴, SaschaWillems/Khronos 샘플)는 `VULKAN_REFERENCES.md`가 다룬다.
- 이 문서는 "**무엇을 구현하는가, 누구를 보고 배우는가, 무엇을 가져다 쓰는가**"(해양 FFT·PBR·TAA·반사·대기 알고리즘 + 라이브러리 + 잘한 게임/논문)를 다룬다.
- 개발 순서는 `docs/ROADMAP.md`, 게임 방향은 `DESIGN.md`, 코드 구조는 `docs/ARCHITECTURE.md`, 남은 엔진 과제는 `docs/ENGINE_TODO.md`.

> **링크 검증:** 아래 URL은 **2026-06-09 WebFetch로 대부분 직접 확인**했다(라이브러리 repo·문서·블로그). 다만 일부 논문 PDF·GDC/SIGGRAPH 발표는 호스팅이 자주 바뀌어 **제목 + 저자 + 발표처(연도)**로만 적었다 — 죽었으면 그 키워드로 검색한다. 발표 원본은 [GDC Vault](https://www.gdcvault.com/), [Advances in Real-Time Rendering](https://advances.realtimerendering.com/), [selfshadow Publications](https://blog.selfshadow.com/publications/)에서 다시 찾는다.

> **제1원칙(불변):** RTX 3060급 / 1080p~1440p / 60fps에서 AAA/AA급 사실적 해양. 레퍼런스는 정답 코드가 아니라 **검증된 패턴 사전**이다. 저사양 편법·화면 도색 wake/foam·노이즈 블롭을 최종 표현으로 넣지 않는다(`CLAUDE.md` §0).

---

## 0. 사용 원칙

- **현재 스택과 연결해서 본다.** 우리는 이미 다중 캐스케이드 Tessendorf FFT + SSR/플래너 반사 + CSM + 선박 PBR + wake 시뮬레이션 마스크를 구현했다(`ARCHITECTURE.md` §2.5). 레퍼런스는 이 기반을 *고도화·안정화*하는 데 쓴다.
- **기법 단위로 본다.** 한 게임/논문을 통째로 따라가지 않고, 우리에게 필요한 부분(예: foam Jacobian, TAA history clamp, ocean BRDF LOD, 부력 hydrostatic force)만 발췌한다.
- **ROADMAP Phase와 매핑한다.** §2 해양·§4 반사·§5 TAA → Phase 4·8, §8 대기·구름 → Phase 7, §10 부력·항해 물리 → Phase 1·6·7, §11 라이브러리 → Phase 8·9.
- **구현 전 문서화한다.** 레퍼런스를 근거로 구조를 바꿀 땐 먼저 `ARCHITECTURE.md`/`ENGINE_TODO.md`에 판단을 남긴다.

---

## 1. 참고 게임 (Study Targets)

우리 장르(해운·항해·교역, UWO 시점)와 화질 목표에 가장 가까운 순서.

| 게임 | 무엇을 배우나 | 비고 |
|---|---|---|
| **Sea of Thieves** (Rare) | 물 표면 셰이딩, foam/거품, 선박-물 상호작용, 스타일과 사실 사이 균형 | 스타일라이즈드지만 물 품질·항해감의 모범. GDC/SIGGRAPH 발표 공개 |
| **Assassin's Creed III / IV: Black Flag** (Ubisoft) | FFT 대양, 다수 선박, wake, 항해 카메라 | 우리 핵심 레퍼런스. 대양 위 선박 연출 |
| **Uncharted 4 / Water Tech** (Naughty Dog) | 물 시뮬레이션·렌더 파이프라인, 상호작용 물 | "Water Technology of Uncharted" GDC 2012 |
| **Crysis / CryEngine** (Crytek) | 굴절, 수면 디테일, 식생, 자연광 | 수면 굴절·식생 애니메이션 GPU Gems 기고 다수 |
| **World of Warships** (Wargaming) / **War Thunder** (Gaijin) | 대형 함선 + 바다, 함선 PBR, 항적, 대규모 해전 | 해군 게임 렌더 발표 존재 |
| **Naval Action / Sea Power / Cold Waters** | 사실적 범선·해군 항해감, 항해 물리 톤 | 게임플레이/항해 물리 톤 레퍼런스 |
| **Horizon Forbidden West / God of War** (PS Studios) | 물/해안, 대기, 톤·컬러 | AAA 자연 연출 기준점 |
| **Ghost of Tsushima** (Sucker Punch) | 바람·식생, 날씨 분위기 | Phase 7~8 바람/식생 참고 |
| **대항해시대 온라인 (UWO)** | **게임 디자인** 레퍼런스(항해/교역/시점) — 그래픽 아님 | `DESIGN.md` 참고 |

공개 발표: **"Wakes, Explosions and Lighting: Interactive Water Simulation in 'Atlas'"** (GDC 2019) — 대양 게임의 **상호작용 물/wake**를 다뤄 우리 wake 마스크와 직접 관련. Sea of Thieves·AC 시리즈 물 발표는 GDC Vault / SIGGRAPH에서 제목으로 검색.

---

## 2. 해양·물 렌더링 (최우선)

화질의 대부분이 여기서 나온다(grazing-angle 시점). 우리 FFT 스택의 이론·고도화 출처. (실제 구현 코드는 §11 라이브러리)

- **Jerry Tessendorf, "Simulating Ocean Water"** (SIGGRAPH course notes, 1999/2001, rev. 2004) — **FFT 대양의 바이블.** Phillips 스펙트럼 h0(k) → 분산관계 애니메이션 → IFFT → choppiness(수평 변위) → **Jacobian 기반 whitecap**. 우리 `ocean_*.comp` 체인의 이론적 출처. (제목으로 검색)
- **Christopher J. Horvath, "Empirical Directional Wave Spectra for Computer Graphics"** (DigiPro 2015) — Phillips보다 나은 방향성 스펙트럼(JONSWAP/TMA), 더 사실적인 파형·바람 연동. 스펙트럼 업그레이드 시 1순위.
- **Eric Bruneton, Fabrice Neyret, Nicolas Holzschuch, "Real-time Realistic Ocean Lighting using Seamless Transitions from Geometry to BRDF"** (Eurographics 2010) — 원거리 수면의 **BRDF/노멀 LOD 필터링**. grazing-angle 윤슬 aliasing(우리 shimmer 문제)을 물리적으로 다룸. TAA(§5)와 함께 볼 것.
- **GPU Gems, Ch.1 "Effective Water Simulation from Physical Models"** (Mark Finch, Cyan Worlds) — Gerstner 파. <https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models>
- **GPU Gems 2, Ch.18 "Using Vertex Texture Displacement for Realistic Water Rendering"** (Yuri Kryachko, Crytek) — displacement 매핑 수면. (developer.nvidia.com/gpugems 에서 검색)
- **GPU Gems 3, Ch.19 "Generic Refraction Simulation"** (Tiago Sousa, Crytek) — scene depth/color 복사 기반 굴절. 우리 water 패스 굴절과 동일 계열.
- **Keith Lantz, "Ocean simulation part one/two"** — <https://www.keithlantz.net/2011/11/ocean-simulation-part-two-using-the-fast-fourier-transform/> — Tessendorf 모델을 DFT→FFT(butterfly, bit-reversal)로 처음부터 구현하는 **C++ 실전 튜토리얼.** 우리 FFT 패스 검증에 좋음.
- **Acerola (Garrett Gunnell), "How Games Fake Water" / FFT Ocean 시리즈** (YouTube) + 코드(§11) — 최신 실전 워크스루.
- **Jump Trajectory, "Ocean waves simulation with Fast Fourier Transform"** (YouTube) — FFT 대양 처음부터 끝까지.
- **Catlike Coding — Flow / Waves** — <https://catlikecoding.com/unity/tutorials/> — 흐름맵/파형 이론(엔진 무관).

거품/whitecap: Jacobian < 0 영역을 foam coverage로(Tessendorf·Horvath·Crest 공통). 우리는 이미 적용 — 해안선 거품/스프레이 확장은 Crest의 foam/spray 모듈(§11) 참고.

---

## 3. PBR · 셰이딩

선박·항구·식생 머티리얼. 우리는 Cook-Torrance GGX 적용 완료 — 일관성·정확도 확장용.

- **selfshadow, "Physically Based Shading in Theory and Practice"** (SIGGRAPH 코스, 2012~2020/2025) — <https://blog.selfshadow.com/publications/> — **PBR의 단일 허브.** 아래 핵심 노트가 연도별 코스 안에 있음:
  - **Brian Karis, "Real Shading in Unreal Engine 4"** (s2013) — UE4 GGX + split-sum IBL. 게임 PBR의 사실상 표준.
  - **Brent Burley, "Physically-Based Shading at Disney"** (s2012) — Disney "principled" BRDF.
  - **Sébastien Lagarde, Charles de Rousiers, "Moving Frostbite to PBR"** (s2014) — 프로덕션 PBR 전환 교과서(에너지 보존, 재질 파라미터, 라이팅).
  - **Naty Hoffman, "Background: Physics and Math of Shading"** — 셰이딩 수학 기초.
- **Google Filament — Materials & Lighting** — <https://google.github.io/filament/> (메인 PBR 문서 `Filament.html`, 재질 시스템 `Materials.html`) — **가장 실용적인 PBR 문서.** 수식·근사·구현이 한 곳에. 셰이더 상수 검증 기준으로 좋음.
- **PBR Book (4th ed., 2023), "Physically Based Rendering: From Theory to Implementation"** — <https://pbr-book.org/> — 무료 전문. 이론 깊이 필요할 때.
- **LearnOpenGL — PBR** — <https://learnopengl.com/PBR/Theory> — 입문/재확인용.

---

## 4. 반사 (SSR / Planar / IBL)

우리는 SSR(temporal) + 플래너 + 분석적 하늘 폴백 적용 완료 — 품질·비용 정책(ROADMAP Phase 4)용.

- **Tomasz Stachowiak, "Stochastic Screen-Space Reflections"** (SIGGRAPH 2015, Advances) — Frostbite SSR. 노이즈/temporal 누적/계층 합성. 우리 SSR 안정화 1순위. (advances.realtimerendering.com 2015 / selfshadow s2015)
- **Kostas Anagnostou, "Screen Space Reflections : Implementation and optimization"** (interplayoflight 블로그) — SSR 레이마치/리파인 실전.
- **Sébastien Lagarde, "Image-based Lighting approaches and parallax-corrected cubemap"** — 큐브맵 IBL/시차 보정. 하늘/환경 반사 폴백 고도화.
- **Karis "Real Shading in UE4"**(§3) — split-sum prefiltered 환경맵(IBL)이 반사 폴백과 직결.
- 플래너 반사: 미러 카메라 + 클리핑 평면(우리 구현 계열).

---

## 5. Temporal AA / 업스케일

물 윤슬 shimmer에 TAA가 핵심(`DESIGN.md`). ROADMAP Phase 4의 주 도입 대상.

- **Brian Karis, "High-Quality Temporal Supersampling"** (SIGGRAPH 2014, Advances in Real-Time Rendering) — <https://advances.realtimerendering.com/> — **TAA의 정전(定典).** Halton jitter, history reprojection, **neighborhood clamp(우리가 계획한 그것)**, ghosting 제어.
- **Lasse Jon Fuglsang Pedersen, "Temporal Reprojection Anti-Aliasing in INSIDE"** (GDC 2016, Playdead) — 실전 TAA + history rectification. **코드·슬라이드 공개:** <https://github.com/playdeadgames/temporal>
- **Marco Salvi, "An Excursion in Temporal Supersampling"** (GDC 2016) — variance clipping 등 clamp 개선.
- **Lei Yang, Shiqiu Liu, Marco Salvi, "A Survey of Temporal Antialiasing Techniques"** (Eurographics 2020 STAR) — TAA 설계 공간 총정리. reactive mask·물 같은 어려운 케이스 판단에 유용.
- **Jorge Jimenez, "Dynamic Temporal Antialiasing and Upsampling in Call of Duty"** (SIGGRAPH 2017) — TAA + 업샘플. 향후 업스케일 검토 시.
- **Bart Wronski 블로그** — <https://bartwronski.com/> — temporal supersampling·reprojection 글 다수. 실전 디테일.
- SMAA 자체(우리 보유)는 Iryoku SMAA 기준 — 세부는 `VULKAN_REFERENCES.md`.

---

## 6. 그림자 (CSM / PCF / PCSS)

우리는 CSM 3캐스케이드 + Poisson PCF 적용 완료 — 품질 옵션·안정화용.

- **Rouslan Dimitrov, "Cascaded Shadow Maps"** (NVIDIA, 2007) — CSM 기본기. 우리 split/stabilization의 출처 계열.
- **Fan Zhang et al., "Parallel-Split Shadow Maps on Programmable GPUs"** (GPU Gems 3, Ch.10) — split scheme 이론.
- **Microsoft, "Common Techniques to Improve Shadow Depth Maps"** — <https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps> — texel snapping, bias, cascade 경계. 실전 체크리스트.
- **Randima Fernando, "Percentage-Closer Soft Shadows" (PCSS)** (NVIDIA, 2005) — 거리 기반 소프트 섀도. 선박/항구 소프트닝 고도화 시.
- **Andrew Lauritzen, "Sample Distribution Shadow Maps" (SDSM)** — 캐스케이드 분포 최적화(원거리 해상 시점에 유리).

---

## 7. 하늘 · 대기 · 구름

우리는 분석적 하늘 + 장거리 fog 적용 — Phase 7 날씨/구름 확장용.

- **Lukas Hosek, Alexander Wilkie, "An Analytic Model for Full Spectral Sky-Dome Radiance"** (ACM TOG / SIGGRAPH 2012) — <https://cgg.mff.cuni.cz/projects/SkylightModelling/> — 분석적 하늘(Preetham 후속). (같은 팀의 2021 신모델도 있음)
- **A. J. Preetham et al., "A Practical Analytic Model for Daylight"** (SIGGRAPH 1999) — 분석적 하늘의 고전.
- **Eric Bruneton, Fabrice Neyret, "Precomputed Atmospheric Scattering"** (EGSR 2008) — 산란 정밀 모델. 수평선/대기 품질이 중요한 해상 시점에 적합.
- **Sébastien Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"** (EGSR 2020) — <https://sebh.github.io/publications/> — 현대적 실시간 대기. 코드/셰이더 공개.
- **Andrew Schneider, "The Real-time Volumetric Cloudscapes of Horizon: Zero Dawn"** (SIGGRAPH 2015) + Nubis 후속 — 볼류메트릭 구름. Phase 7 날씨(storm/cloud) 1순위.

---

## 8. 후처리 · 톤매핑 · 컬러

우리는 HDR(R16F) + ACES + 그레이딩 적용 — 색·노출 일관성용.

- **John Hable, "Uncharted 2: HDR Lighting"** (GDC 2010) + <https://filmicworlds.com/> — 필름릭 톤매핑의 고전.
- **Krzysztof Narkowicz, "ACES Filmic Tone Mapping Curve"** (2016) — <https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/> — 우리가 쓰는 ACES 근사식 계열(HLSL, CC0/MIT).
- **Stephen Hill, "ACES Fit"** — selfshadow에 정리된 RRT+ODT 근사. ACES 정확도 확장 시.
- **Troy Sobotka, "AgX"** — <https://github.com/sobotka/AgX> — 최신 대안 톤매핑(하이라이트 처리 우수). 윤슬/태양 glint 과노출 검토 시 비교.

---

## 9. 식생 · 바람 (Phase 7~8)

항구/섬 식생, 바람 연출. 농장 grass 시스템으로 회귀하지 않고 새로 본다.

- **GPU Gems 3, Ch.16 "Vegetation Procedural Animation and Shading in Crysis"** (Tiago Sousa) — 바람 애니메이션·식생 셰이딩의 고전.
- **Sucker Punch, Ghost of Tsushima 발표** — "Samurai Shading"(SIGGRAPH 2020) / 바람·날씨 GDC 발표 — 바람 필드와 풀/나무 연출.
- **Acerola / Ben Cloward (YouTube)** — 식생·바람·물 셰이더 워크스루(엔진 무관).

---

## 10. 선박 부력 · 항해 물리 (Phase 6 · 7)

게임 물리지만 향후 선박 성장·항해 심화(ROADMAP Phase 6~7)에 직접 쓰인다. 현재는 `ShipState` 기반 2D 항해 물리와 GPU 5점 FFT 부력 샘플로 선박을 파면에 얹는 수준이며, 아래는 그 위에 **힘 기반 선체 모델**을 올릴 때의 출처다.

- **Jacques Kerner (Avalanche Studios), "Water interaction model for boats in video games"** (Game Developer / Gamasutra, 2015) — <https://www.gamedeveloper.com/programming/water-interaction-model-for-boats-in-video-games> — **게임 보트 물리의 표준 레퍼런스.**
  - Part 1: **hydrostatic(부력) 힘** — 침수 다각형(submerged triangles) 기반 부력·복원 모멘트. 향후 3D 선체 부력/기울임 고도화에 직접 적용.
  - Part 2~3: **동적 힘** — 항력(drag), 양력, slamming, viscous/pressure 힘. Phase 6/7 선체 hydrodynamics(lateral slip, 선회, 속도-항력)와 연결.
- 보조: 강체 적분(velocity/angular velocity), 관성 텐서 — 일반 게임 물리 교재(`Real-Time Rendering`/`Game Physics` 계열) 참고. 현재 1차 항해는 단순 2D 모델이고, Kerner 모델은 3D 선체로 확장할 때 도입한다.

---

## 11. 라이브러리 · 오픈소스 구현

가져다 쓰거나 코드를 직접 읽을 수 있는 것. **도입은 별도 승인**(새 의존성, `CLAUDE.md` §6) 후 — 여기서는 후보로만 정리.

### Vulkan 엔진 유틸 (Phase 9)

- **VulkanMemoryAllocator (VMA, AMD)** — <https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator> — Vulkan 메모리 할당 라이브러리(MIT). `ENGINE_TODO.md`의 DEVICE_LOCAL + staging, deletion queue, 버퍼/이미지 메모리 관리를 표준화. Blender·BG3·Cyberpunk·Godot 등 채택.
- **volk** — <https://github.com/zeux/volk> — Vulkan 메타 로더(MIT). 엔트리포인트 동적 로드, 확장 함수 포인터 자동화. `vulkan-1.dll` 정적 링크 회피.
- **SPIRV-Reflect (Khronos)** — <https://github.com/KhronosGroup/SPIRV-Reflect> — SPIR-V 리플렉션(Apache 2.0). descriptor binding/세트/푸시상수 레이아웃을 셰이더에서 추출 → Phase 9 "descriptor 관리 정리 + C++↔GLSL desync 제거"에 직접 유용.

### 에셋 파이프라인 (Phase 8)

- **KTX-Software (Khronos)** — <https://github.com/KhronosGroup/KTX-Software> — KTX2 텍스처 컨테이너 + BCn/Basis(ETC1S/UASTC) + 밉맵 + Zstd. 현재 DDS BC1 경로를 표준 KTX2로 확장할 때.
- **meshoptimizer (zeux)** — <https://github.com/zeux/meshoptimizer> — 메시 최적화/단순화/LOD 생성/버텍스 캐시/압축(MIT). 선박·항구·섬 LOD에 유용. `gltfpack` 동봉.
- **cgltf (jkuhlmann)** — <https://github.com/jkuhlmann/cgltf> — 단일 헤더 glTF 2.0 로더(MIT). Phase 8에서 OBJ/하드코딩 선박을 glTF로 옮길 때 후보(또는 `tinygltf`).
- 이미 사용 중: **Dear ImGui**(DevUI), **stb_image**(텍스처 로드).

### 해양 오픈소스 구현 (직접 코드 학습용)

- **Crest Ocean System** — <https://github.com/wave-harmonic/crest> — class-leading 물 시스템(Unity, FFT + foam + flow + 동적 상호작용 파도). **실전 구조 참고로 최고.** 문서 crest.readthedocs.io.
- **Garrett Gunnell (Acerola), "Water"** — <https://github.com/GarrettGunnell/Water> — Sum-of-sines / FBM / **FFT(Dual JONSWAP 4밴드)** + microfacet/SSS 셰이더. 교육 영상 동반. 우리 캐스케이드 FFT와 가장 비슷한 학습 코드.
- **gasgiant, "FFT-Ocean" / "Ocean-URP"** — <https://github.com/gasgiant/FFT-Ocean> (교육용 프로토타입), <https://github.com/gasgiant/Ocean-URP> (개선판: geomorphing·수중·셰이더). Unity FFT 대양 구현 참고.
- **NVIDIA WaveWorks** — 상용 FFT 대양 라이브러리(공개 GitHub 없음, 등록 필요) — 기능 범위 참고만.

> 라이선스 주의: 위 Unity 구현들은 셰이더/알고리즘 **학습**용으로 본다. 코드 이식은 라이선스(MIT 등) 확인 후, `docs/ROADMAP.md` Phase 8 에셋 정책에 따라 출처를 기록한다.

---

## 12. Vulkan API · 엔진 (→ VULKAN_REFERENCES.md)

"이 기법을 Vulkan으로 *어떻게 배선하나*"는 `VULKAN_REFERENCES.md`가 단일 출처다. 핵심만:

- **Khronos Vulkan-Samples** — <https://github.com/KhronosGroup/Vulkan-Samples>
- **SaschaWillems/Vulkan** — <https://github.com/SaschaWillems/Vulkan>
- **vkguide.dev** — <https://vkguide.dev/> — 현대 Vulkan(동적 렌더링/디스크립터) 실전 가이드
- **Vulkan Docs (Khronos)** — <https://docs.vulkan.org/>
- **GPUOpen (AMD)** — <https://gpuopen.com/> — 성능·메모리·배리어 best practice (VMA·§11 포함)

---

## 13. 종합 허브 · 서적 · 블로그

막힐 때 먼저 뒤지는 곳.

- **Advances in Real-Time Rendering in Games** (SIGGRAPH 코스, 2006~) — <https://advances.realtimerendering.com/> — **AAA 렌더 발표의 금광.** TAA·SSR·물·구름·그림자 핵심이 매년 여기.
- **selfshadow Publications** — <https://blog.selfshadow.com/publications/> — PBR/셰이딩 코스 허브.
- **Real-Time Rendering, 4th Ed.** (Akenine-Möller et al.) — <https://www.realtimerendering.com/> — 표준 교과서 + 링크 모음.
- **GPU Gems 1 / 2 / 3** (NVIDIA, 무료) — <https://developer.nvidia.com/gpugems/gpugems/> — 물·식생·그림자 고전 챕터 다수.
- **GPU Pro / GPU Zen** 시리즈 (서적) — 실전 기법 모음.
- **Inigo Quilez** — <https://iquilezles.org/articles/> — 노이즈·SDF·물·useful math.
- **Bart Wronski** — <https://bartwronski.com/> — temporal/샘플링/이미지 처리 심화.
- **The Book of Shaders** — <https://thebookofshaders.com/> — GLSL/노이즈 기초.
- **GDC Vault** — <https://www.gdcvault.com/> — 위 게임들의 발표 원본.
- **YouTube**: Acerola, Ben Cloward, Freya Holmér, SimonDev — 기법 워크스루.

---

## 문서 갱신 규칙

- 새 그래픽 기법/논문/라이브러리/게임 레퍼런스 판단은 이 문서에 적는다(Vulkan API 배선 패턴은 `VULKAN_REFERENCES.md`).
- 라이브러리를 실제로 도입하면 새 의존성이므로 **사용자 승인 + `docs/ROADMAP.md` Phase 8 에셋/라이선스 정책**을 따른다.
- 구조 결정으로 굳으면 `ARCHITECTURE.md`에 짧게 승격하고, 남은 작업은 `ENGINE_TODO.md`/`ROADMAP.md`에 연결한다.
- 실제 구현 완료·검증은 `DEVLOG.md`에 남긴다.
- 게임 비주얼 방향 자체가 바뀔 때만 `DESIGN.md`를 수정한다.
