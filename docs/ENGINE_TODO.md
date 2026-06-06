# 엔진 TODO

이 문서는 OceanVoyage 엔진 수준의 작업을 추적한다. 농장 게임 전용 문제가 아니라, Vulkan 기반 자체 엔진의 공통 기반 품질·안정성·성능과 관련된 항목이다.

> 갱신: 2026-06-06. 초기 P0/P1 항목 대부분은 바다 전환 과정에서 완료됐다. 아래는 완료분과 남은 과제, 그리고 현재 코드 리뷰에서 확인한 한계를 분리해 정리한다.

---

## 완료됨 (2026-06-06 기준)

**안정성**
- ✅ 공통 `vkCheck` 헬퍼 + 주요 경로(커맨드버퍼/큐 submit/fence/memory map/리소스 생성) 반환값 검사
- ✅ 저장/로드 안정성: 세이브 v3(atomic write + 범위/enum 검증 + 전체 성공 시에만 커밋)
- ✅ dt 클램프(0.1s)로 stall 후 점프/터널링 방지
- ✅ 디바이스/스왑체인 적합성 검사, 스왑체인 재생성 시 per-image 세마포어 재생성

**렌더링 기본 품질**
- ✅ albedo `*_SRGB` / mask·opacity·roughness UNORM 분리 로드
- ✅ 일반 텍스처 + `sampler2DArray` 밉맵 생성, trilinear 필터링
- ✅ anisotropic 필터링(디바이스 feature 확인 후 활성, 한도 클램프)
- ✅ 기본 AA를 SMAA로 변경 (OFF/FXAA/SMAA 선택)
- ✅ HDR scene color target(`R16G16B16A16_SFLOAT`) + ACES 톤매핑 + 그레이딩

**해양 렌더링** (구 P3 "향후 요구사항" — 전부 구현됨)
- ✅ ocean material/pass (전용 water 패스, depth/color 분리)
- ✅ animated water normal (FFT slope 맵 + 다중 스케일 타일 노멀)
- ✅ 파도 변위 (Gerstner → 다중 캐스케이드 Tessendorf FFT)
- ✅ reflection (SSR + temporal reprojection + 플래너 + 분석적 하늘)
- ✅ fog / horizon blending (장거리 대기 fog)
- ✅ ship wake rendering (시뮬레이션 마스크 기반)
- ✅ CSM 3 캐스케이드 + Poisson PCF, grass 캐스터는 비활성 결정

---

## 남은 과제

### P1: 렌더링 품질 후속

- **TAA 도입 검토.** `DESIGN.md`가 "물에는 TAA가 더 적합"이라 명시했고, SSR 히스토리용 `prevViewProj`/`temporalParams` 배선은 이미 존재한다. 다만 현재 배선은 물 SSR history 안정화 전용이며, 전체 화면 TAA resolve/jitter/motion vector/reactive mask는 없다. FFT 스펙큘러 윤슬 shimmer는 SMAA/FXAA만으로 잡기 어렵다.
- **SMAA 순서/색공간 정리.** 현재 `smaa_edge.frag`는 HDR scene color를 ACES 톤매핑 + 감마 luma로 변환해 edge를 검출하므로 "HDR-linear edge 검출" 상태는 아니다. 그러나 neighborhood pass는 여전히 HDR scene color를 먼저 섞은 뒤 톤매핑/그레이딩한다. 더 표준적인 구조는 별도 tone-map/grade target을 만든 뒤 그 perceptual LDR 입력에 SMAA를 적용하는 것이다.
- **셰이더 상수 단일 출처화.** `CASCADE_L`, `N=512`, `LOG2N=9`, `WAKE_N`/`WAKE_WORLD_SIZE`, `SEA_LEVEL`이 CPU 헤더 + 4개 이상 셰이더에 "must match" 주석과 함께 리터럴로 중복된다. specialization constant 또는 생성 헤더로 통합.

### P2: 성능 (RTX 3060 예산에선 여유 있으나 정공법 아님)

- **async compute 검토.** 해양 FFT(스펙트럼 업데이트 + 18 IFFT 패스 + assemble + wake)가 그래픽스 큐에서 배리어로 직렬화된다. 현재 큐 모델은 graphics/present만 관리하므로, 전용 compute 큐 도입은 queue family 선택·command pool·ownership/sync까지 함께 설계해야 한다.
- **부력 리드백 축소.** 선박 1척을 띄우기 위해 displacement 전체(512²×3 RGBA16F = 6,291,456 bytes, 약 6.0 MiB/프레임)를 host 복사한다. 필요한 작은 영역/소수 텍셀만 복사하거나 GPU 측 샘플링/축약 버퍼를 검토한다.
- **플래너 반사 + SSR 비용 재검토.** 둘 다 매 프레임이고 플래너는 현재 청크/오브젝트/풀 경로까지 통과할 수 있는 전체 씬 재렌더다. 기본 해상 테스트 월드는 물 타일만 생성해 실제 농장 콘텐츠가 대량 렌더되는 상태는 아니지만, 비용 정책 없이 항상 켜진 구조는 장기적으로 부담이다.
- **바다 메시 재검토.** 고정 약 60만 삼각형 방사형 팬(512 섹터 고정, 통째 draw). projected-grid/clipmap 계열이 더 표준적·확장적이며, ocean vertex/index buffer도 DEVICE_LOCAL + staging 업로드 대상으로 본다.

### P2: 그림자

- 셰이더에 하드코딩된 shadow map size 제거 (`ship.frag`의 `SHADOW_MAP_TEXEL = 1/2048` 등) → uniform/push constant로 전달
- 그림자 품질 옵션(Low 1024 / Medium 2048 / High 4096)
- 비활성화된 grass shadow 파이프라인/리소스 정리(`kGrassCastsShadow=false` 확정 시 제거)

### P2: GPU 리소스 관리

- 정적/반정적 mesh buffer를 DEVICE_LOCAL로 이동 + staging 업로드(현재 ocean/일부 mesh는 HOST_VISIBLE 매핑)
- object/grass instance data에 staging 또는 ring-buffer 전략
- 작은 per-frame uniform은 HOST_VISIBLE 유지(현 상태 유지)
- buffer usage policy 문서화

### P2: 시작 / 로딩

- 첫 present 전 동기 작업 정리(현재 FFT 초기화 전이마다 fence wait)
- pipeline cache 추가/활성화

### P3: 안정성 (낮은 우선순위)

- VulkanContext 초기화 실패 시 부분 생성 리소스 정리(현재는 throw 후 프로세스 종료 — 허용 가능하나 개선 여지)
- `VulkanContext_Init.cpp`(4000+줄) one-shot 커맨드버퍼/배리어 보일러플레이트를 `begin/endSingleTimeCommands`로 추출

### P3: 향후 해양/대기 고도화

- wake 스프레이 파티클, 해안선 거품
- port lighting
- weather / 동적 시간대 연동 강화
