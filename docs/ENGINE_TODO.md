# 엔진 TODO

이 문서는 OceanVoyage 엔진 수준의 작업을 추적한다. 농장 게임 전용 문제가 아니라, Vulkan 기반 자체 엔진의 공통 기반 품질·안정성·성능과 관련된 항목이다.

> 갱신: 2026-06-12. 초기 P0/P1 항목 대부분은 바다 전환 과정에서 완료됐다. 아래는 완료분과 남은 과제, 그리고 현재 코드 리뷰에서 확인한 한계를 분리해 정리한다. 2026-06-11 농장 레거시 완전 제거(`World`/`Chunk`/grass/`Player` 소멸), 2026-06-12 Phase 4 핵심 렌더 작업(TAA 1차, SMAA 색공간, 부력 리드백 축소, 반사 모드, shared constants)을 반영했다.
>
> 이 문서는 엔진/렌더 기술 과제의 단일 출처다. 이 과제들이 전체 개발 순서 어디에 들어가는지는 `docs/ROADMAP.md` Phase 4(렌더링 후속)·Phase 9(기술 부채/구조 안정화)를 본다.

---

## 완료됨 (2026-06-12 기준)

**안정성**
- ✅ 공통 `vkCheck` 헬퍼 + 주요 경로(커맨드버퍼/큐 submit/fence/memory map/리소스 생성) 반환값 검사
- ✅ 저장/로드 안정성: atomic write + 검증 + 전체 성공 시에만 커밋 (농장 세이브 v3 → 2026-06-11 `VoyageSave` OVYG v1로 교체, 같은 안전 패턴 계승 + finite-float 검증 추가)
- ✅ dt 클램프(0.1s)로 stall 후 점프/터널링 방지
- ✅ 디바이스/스왑체인 적합성 검사, 스왑체인 재생성 시 per-image 세마포어 재생성

**렌더링 기본 품질**
- ✅ albedo `*_SRGB` / mask·opacity·roughness UNORM 분리 로드
- ✅ 일반 텍스처 + `sampler2DArray` 밉맵 생성, trilinear 필터링
- ✅ anisotropic 필터링(디바이스 feature 확인 후 활성, 한도 클램프)
- ✅ 기본 AA를 SMAA로 변경 (OFF/FXAA/SMAA 선택)
- ✅ HDR scene color target(`R16G16B16A16_SFLOAT`) + ACES 톤매핑 + 그레이딩
- ✅ TAA 1차(resolve 패스, aaMode 3 opt-in) 구현. 기본 AA는 SMAA 유지
- ✅ SMAA 순서/색공간 정리: 톤매핑+그레이딩 LDR 타겟 이후 SMAA 적용
- ✅ 셰이더 상수 단일 출처화: `shaders/shared_constants.h`

**해양 렌더링** (구 P3 "향후 요구사항" — 전부 구현됨)
- ✅ ocean material/pass (전용 water 패스, depth/color 분리)
- ✅ animated water normal (FFT slope 맵 + 다중 스케일 타일 노멀)
- ✅ 파도 변위 (Gerstner → 다중 캐스케이드 Tessendorf FFT)
- ✅ reflection (SSR + temporal reprojection + 플래너 + 분석적 하늘)
- ✅ fog / horizon blending (장거리 대기 fog)
- ✅ ship wake rendering (시뮬레이션 마스크 기반)
- ✅ CSM 3 캐스케이드 + Poisson PCF, grass 캐스터는 비활성 결정
- ✅ 부력 리드백 축소: 전체 displacement 복사 대신 GPU 5점 샘플 + 20B host-visible 버퍼
- ✅ 반사 모드 4종(SKY/SSR/PLANAR/FULL)과 셰이더/패스 게이트

---

## 남은 과제

### P1: 렌더링 품질 후속

- **TAA 후속(보류 중).** 1차 resolve 패스는 구현됨(2026-06-12, aaMode 3 옵션 — 재투영+neighborhood clamp, 윤슬 shimmer 안정화 확인). **남은 것**: Halton 지터, Catmull-Rom 히스토리 샘플링, 모션 적응 블렌드(+샤프닝) — 이동 중 bilinear 재샘플 블러 해결용 표준 3종. 사용자 결정(2026-06-12)으로 보류; 기본 AA는 SMAA, TAA는 opt-in. 재개 기준: "정지 화면이 SMAA보다 선명 + 이동 중 블러 대폭 감소" 달성 여부로 채택 재평가.
- **물/선박 temporal 품질 재평가.** TAA는 opt-in 상태라 기본 경로는 SMAA다. TAA 2차를 재개한다면 reactive mask/샤프닝/모션 적응까지 묶어 "움직일 때도 선명한가"를 기준으로 본다.

### P2: 성능 (RTX 3060 예산에선 여유 있으나 정공법 아님)

- **async compute 검토.** 해양 FFT(스펙트럼 업데이트 + 18 IFFT 패스 + assemble + wake)가 그래픽스 큐에서 배리어로 직렬화된다. 현재 큐 모델은 graphics/present만 관리하므로, 전용 compute 큐 도입은 queue family 선택·command pool·ownership/sync까지 함께 설계해야 한다.
- **반사 세부 비용 정책.** REFL 모드 4종은 구현됐지만, 플래너 반해상도/대상 제한, SSR step 품질 옵션은 아직 사용자 결정이 필요한 시각 트레이드오프다. 항구/섬 콘텐츠가 늘면 실제 비용을 측정한 뒤 조정한다.
- **바다 메시 재검토.** 고정 약 60만 삼각형 방사형 팬(512 섹터 고정, 통째 draw). projected-grid/clipmap 계열이 더 표준적·확장적이며, ocean vertex/index buffer도 DEVICE_LOCAL + staging 업로드 대상으로 본다.

### P2: 그림자

- shadow map size 하드코딩은 `shared_constants.h`로 통합됐다. 남은 과제는 런타임 그림자 품질 옵션(Low 1024 / Medium 2048 / High 4096)과 옵션 변경 시 리소스 재생성 정책이다.
- ~~비활성화된 grass shadow 파이프라인/리소스 정리~~ ✅ 2d-1b에서 제거 완료(2026-06-09)

### P2: GPU 리소스 관리

- 정적/반정적 mesh buffer를 DEVICE_LOCAL로 이동 + staging 업로드(현재 ocean/ship mesh는 HOST_VISIBLE 매핑)
- ~~object/grass instance data 전략~~ — 해당 경로 제거됨(2d). 향후 port/island 인스턴싱 도입 시 staging/ring-buffer로 설계
- 작은 per-frame uniform은 HOST_VISIBLE 유지(현 상태 유지)
- buffer usage policy 문서화

### P2: 시작 / 로딩

- 첫 present 전 동기 작업 정리(현재 FFT 초기화 전이마다 fence wait)
- pipeline cache 추가/활성화

### P3: 안정성 (낮은 우선순위)

- VulkanContext 초기화 실패 시 부분 생성 리소스 정리(현재는 throw 후 프로세스 종료 — 허용 가능하나 개선 여지)
- `VulkanContext_Init.cpp`(4000+줄) one-shot 커맨드버퍼/배리어 보일러플레이트를 `begin/endSingleTimeCommands`로 추출

### P3: 엔진 구조·디버깅·테스트 (ROADMAP Phase 9)

큰 기능 교체가 안정된 뒤 진행한다. 모두 기능 변화 없이, 작은 diff로.

- **VulkanContext 파일 분리.** `VulkanContext_Init.cpp`를 기능별(Swapchain/Pipelines/Textures/Shadow/Post/Dev)로 점진 분리. 공통 private struct는 `VulkanContext_Private.h` 유지. texture helper 또는 post/SMAA처럼 독립적인 것부터.
- **descriptor 관리 정리.** scene/reflection/ocean/post/SMAA/shadow/ship descriptor set layout과 binding 번호 문서화 → C++↔GLSL desync 위험 제거. 반복 `VkWriteDescriptorSet` 패턴 정리(과한 추상화 금지). descriptor pool sizing을 frame-in-flight 기준으로.
- **debug utils / RenderDoc.** 커맨드버퍼 region label(Ocean FFT/Shadow/Planar/Opaque/Water/Post/UI) + 주요 image/buffer/pipeline object name(dev 빌드 한정). validation warning 0 목표(known false positive 제외).
- **dev profiling 확대.** GPU timestamp 구간을 ocean compute/shadow/reflection/opaque/water/post/UI로 세분, CPU timing(update/snapshot/record), draw count, 토글별(SSR/Planar/TAA/SMAA) 비용 비교.
- **pure logic 테스트.** ship physics step / cargo capacity / market buy-sell / save-load 검증 / wind angle efficiency를 렌더 실행 없이 검증하는 작은 test executable(새 의존성 없이, golden 값). CI는 후순위.
- **save migration 정책.** version table(v1 ship → v2 cargo/money → v3 ports/market → v4 upgrades/contracts), corruption test(truncated/wrong magic/bad count/NaN/huge). 구 PFRM 명시 거부는 완료(OVYG magic 불일치, 2026-06-11) — version table·corruption test가 남은 과제.
- **품질 tier.** High(RTX 3060 기본) / Medium(fallback) / Low(디버그·호환용, 최종 지향 아님). 옵션: shadow size, reflection mode, SSR steps, TAA on/off, planar half-res, ocean mesh quality. 품질을 싸게 낮추는 게 아니라 비싼 기능을 명시적으로 tier화. 옵션 변경 시 swapchain recreate 안정성 확인.
- ~~**legacy farm code 제거.**~~ ✅ 완료(2026-06-11): `World`/`Chunk`/`TerrainGen`/`Player`/`ItemType`/`TileType` 소스에서 소멸(4개 슬라이스, DEVLOG 참고). 남은 건 `assets/` 내 미사용 farm asset 확인 정도.

### P3: 향후 해양/대기 고도화

- wake 스프레이 파티클, 해안선 거품(shore distance + wave energy, mask/advection 기반 — 단순 노이즈 띠 금지)
- port lighting(등대 beacon, point light 수 제한, emissive 발광)
- weather / 동적 시간대 연동 강화(wind→roughness/whitecap/wave detail, storm visibility)
- 플래너 해상도·대상 제한 + SSR step 옵션(REFL 모드 4종은 구현됨)
