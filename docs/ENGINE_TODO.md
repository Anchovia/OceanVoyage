# 엔진 TODO

이 문서는 OceanVoyage 전환 전후로 정리해야 할 엔진 수준 문제를 추적한다.

여기 있는 항목은 농장 게임 전용 문제가 아니라, Vulkan 기반 자체 엔진의 공통 기반 품질과 관련된 작업이다.

## P0: 안정성

* Vulkan 결과값 처리를 위한 공통 `VK_CHECK` 헬퍼 추가
* command buffer, queue submit, fence, memory map, 리소스 생성 경로의 반환값 검사
* VulkanContext 초기화 실패 시 부분 생성된 리소스 정리 개선
* 전환 과정에서도 항상 빌드 가능한 상태 유지
* 게임 시스템 교체 중에도 저장 / 로드 안정성 유지

## P1: 렌더링 기본 품질

* authored color / albedo 텍스처가 sRGB로 로드되는지 확인
* mask, opacity, roughness, AO 같은 비색상 데이터가 UNORM으로 로드되는지 확인
* 일반 텍스처와 texture array의 mipmap 생성 확인
* 필요한 곳에 trilinear filtering 적용 확인
* anisotropic filtering은 디바이스 feature 지원 확인 후 활성화
* 기본 AA 모드를 OFF에서 FXAA 또는 SMAA로 변경
* 셰이더에 하드코딩된 shadow texel size 제거
* 그림자 품질 설정 추가
* HDR scene color target 검토
  예: `VK_FORMAT_R16G16B16A16_SFLOAT`

## P2: GPU 리소스 관리

* 정적 / 반정적 mesh buffer를 DEVICE_LOCAL 메모리로 이동
* chunk mesh 업로드에 staging buffer 사용
* object / grass instance data에 staging 또는 ring-buffer 전략 적용
* 작은 per-frame uniform data는 HOST_VISIBLE 유지
* buffer usage policy 문서화

## P2: 시작 / 로딩

* 첫 present 전 지나치게 많은 동기 작업을 하지 않도록 정리
* 초기 리소스 로딩 화면 또는 로딩 단계 추가
* 게임 시작 전에 필수 가시 리소스가 준비되도록 처리
* pipeline cache 추가 또는 활성화

## P2: 그림자

* 셰이더에 하드코딩된 shadow map size 제거
* shadow map size, texel size, PCF radius를 uniform 또는 push constant로 전달
* 그림자 품질 옵션 추가
  예: Low 1024 / Medium 2048 / High 4096
* grass가 그림자를 드리울지 결정
* grass shadow를 계속 끌 거라면 사용하지 않는 grass shadow pipeline 리소스 제거

## P2: 식생 / 알파

* grass alpha edge 품질 개선
* MSAA를 도입할 경우 alpha-to-coverage 검토
* grass translucency 또는 backlight 추가
* 식생 주변 contact AO 또는 ground darkening 추가

## P3: 향후 바다 렌더링 요구사항

지금 바로 구현할 항목은 아니지만, 엔진 정리에 영향을 주는 미래 요구사항이다.

* ocean material / pass
* animated water normal
* Gerstner wave
* reflection strategy
* fog / horizon blending
* ship wake rendering
* port lighting
* weather / time-of-day 연동
