# semantics-ar — 외부검증 보고서 (External Validation)

> 목적: "복원 가능한 데이터 파괴 암호화 공격"에 한정하여, (Q1) FN=0 방어 로직이
> 진정으로 완결되었는지, (Q2) FP 최소화/자원 효율 로직이 최적에 닿아 있는지를,
> **실제 프론티어 공개 구현체 및 학술 프론티어와 직접 대조**하여 객관적으로 평가한다.
> 반례는 와이퍼·특수 하드웨어 등 논점 이탈을 배제하고, 랜섬웨어가 실제로 활용 가능한
> 경로만 대상으로 한다. 근거는 코드(driver/, engine/)와 외부 1차 자료(아래 각주)이다.

> **권위 문서 주의(중요):** `docs/CONSTITUTION.md`의 **III.5 / Part IV 는 레거시**이며
> *폐기된* whole-file-at-open 설계를 서술한다(HANDOFF §1·§7.2: "Constitution rewrite … still
> legacy"). **채택된 설계는 `docs/DESIGN_REVIEW_PRESERVATION.md` + `HANDOFF.md`**(비대칭·지연·
> 영역단위 capture)이다. 따라서 본 평가는 채택 설계를 기준으로 하며, 초판에서 "헌장 vs 코드
> 불일치"로 적었던 항목(구 §3.4)은 **findings가 아니라 이미 예정된 헌장 재작성 대상**이므로
> 철회한다. 아래 findings는 모두 **코드 기반**이라 그대로 유효하되, 채택 설계 언어로 재기술한다.

---

## 0. 결론 (verdict)

- **아키텍처 등급**: semantics-ar은 학술 프론티어(ShieldFS/Redemption/PayBreak)의 세 축을
  **한 시스템에 통합**하고, 그 중 두 축(탐지 신호·보존 정밀도)에서 프론티어를 **앞선다**.
  공개 실물 구현(RansomGuard)과 비교하면 세대 차가 난다. 이는 과장이 아니라 코드로 확인된다.
- **Q1 (FN=0)**: *관측된 실제 랜섬웨어 모집단*에 대해, 그리고 *보존 예산(용량·보존기간) 내부*에서는
  FN=0이 실질적으로 성립한다. 그러나 사용자가 내건 **무조건적** 명제 —
  "Oracle과 Phantom이 **완전히** 우회되어도 단 한 파일도 피해 없음" — 은 보존 단독으로는
  **엄밀히 성립하지 않는다.** 유일하게 실재하는 예산-초과 벌크 공격의 **fail-open 축출(eviction)**
  경로가 남으며(§3.1), 이는 설계 자신의 Part IX 한계와 일치하지만 "한계 도달 시 차단"이라는
  표현이 실제 메커니즘보다 강하다. 이는 결함이 아니라 **명제의 과대 진술**이다.
- **Q2 (효율/FP)**: FP 최소화 스택은 프론티어 수준 이상이다(특히 게이트 신호). 다만
  "보안 개입을 인지조차 못 함"은 **쓰기 집약 워크로드에서는 아직 아스피레이션**이며,
  그 진위는 전적으로 write-through 암호화 저장소 처리율(미측정)에 달려 있다. 최적성을 깨는
  구체적 지점은 두 가지(§4)뿐이며 모두 개선 가능하다.

---

## 1. 대상 이해 요약 (검증 기준선)

세 개의 증거 채널이 병렬로 동작하며, 응답은 증거 확실성에 비례한다.

| 채널 | 증거 | 자산 | 코드 |
|---|---|---|---|
| Oracle | 순방향 증명 `Enc_K(원본)==기록` (키를 쓰기 프로세스 메모리에서 지연 스냅샷) | 정의적·무한 복구 | `driver/capture.c`, `engine/src/battery.c` |
| Phantom | 보이지 않는 미끼에 대한 K≥3 콘텐츠 쓰기(누적 행위 증거) | 정황·복구 아님 | `driver/phantom.c` |
| Preserve | 파괴 직전 원본을 영역 단위로 보존(probation→conviction시 protected) | 정황·유계 복구 | `driver/preserve.c`, `engine/src/preserve.c` |
| Gate D∧T | diff-제한 조건부 novelty (2-gram coverage) — 차단 안 함, 후보만 | 비용 필터 | `engine/src/gate.c` |

핵심 전제(사용자가 "읽기 선행"으로 요약): **인플레이스로 원본을 파괴하려면 그 원본을
읽는(또는 쓰기 의도로 여는) 선행 행위가 반드시 존재**하며, 보존은 그 open 시점(파괴 이전)에서
원본을 확보한다. Phantom만 이 전제의 예외(미끼는 읽기와 무관하게 행위를 함정).

---

## 2. 프론티어 좌표 (요청된 외부 대조)

### 2.1 실물 공개 구현 — RansomGuard (`0mWindyBug/RansomGuard`)

> 주: 프롬프트에 언급된 `Muirey03/RansomGuard`는 존재하지 않는다(404). 서술과 일치하는
> 실제 프로젝트는 `0mWindyBug/RansomGuard`이며, 아래는 그 소스 기반 사실이다.

| 항목 | RansomGuard | semantics-ar |
|---|---|---|
| 탐지 신호 | **Shannon 엔트로피 델타** (`ENTROPY_ENCRYPTED=7.991`, 계수 0.83) | **조건부 novelty** (2-gram, θ=0.10, μ=12) |
| 키 복구 | **없음** | Oracle (메모리 키-스케줄 스캔 + 배터리 순방향 증명) |
| 미끼 | **없음** | Phantom(가상 파일 4계층 IRP 폐색) |
| 보존 | 전체 파일 COW 백업(확정 시 디스크 커밋) | **영역 단위** 보존 + 삭제/rename엔 **하드링크** |
| 차단 | 6회 암호화 시 **프로세스 강제 종료(`ZwTerminateProcess`)** | 종료 없이 파괴 경로만 STATUS_ACCESS_DENIED |
| 절단-생성 처리 | PreCreate에서 `(Options>>24)&0xff` 판별 후 스냅샷 | **동일 기법**(`SarCaptureSubmitSupersede`) |

시사점: 두 프로젝트가 **독립적으로 동일한 PreCreate 절단-생성(disposition) 스냅샷 기법**에
수렴했다는 사실은, semantics-ar의 create-time 처리가 프론티어 관행임을 교차검증한다.
결정적 차이는 RansomGuard가 엔트로피 델타(부분 암호화에 취약, 고엔트로피 원본에 FP)에
의존하고 키 복구·미끼가 없다는 점 — semantics-ar이 세대 앞선다.

### 2.2 학술 프론티어

- **Redemption (RAID'17)** — *redirect-on-write*: 모든 쓰기를 sparse "reflected file"로 전환,
  원본은 양성 판정 커밋 전까지 **결코 건드리지 않음**. "zero data loss"를 주장하되
  저자 스스로 저속 공격(6시간에 1파일)에서는 보장 불가라고 명시. 오버헤드 2.6%.
  → **구조적으로 가장 강함**: "백업 vs 원본" 경쟁 자체가 없다.
- **ShieldFS (ACSAC'16)** — IRP_MJ_CREATE(쓰기/삭제 security context)에서 **첫 쓰기 전에**
  선제 COW 섀도. 악성 판정 시 복원. **semantics-ar과 동일 계열.** 한계: T시간 모방 공격
  ("victim will lose the original copies"). CryptoFinder로 AES 스케줄 메모리 스캔도 수행.
- **PayBreak (AsiaCCS'17)** — 크립토 API 후킹으로 키 에스크로. **20개 중 12개 패밀리만 복구**
  (정적 링크/커스텀 크립토를 놓침).
- **CryptoDrop / RWGuard** — 탐지 위주. 탐지 전 손실 불가피(CryptoDrop 중앙값 10파일,
  RWGuard 제3자 추정 ~288파일).

**좌표 결론**: semantics-ar = **ShieldFS 계열 보존 + PayBreak 계열 키복구 + RWGuard/UNVEIL 계열
미끼**의 통합. Redemption만이 원본을 아예 파괴하지 않는 유일한 구조다. 따라서 문자 그대로의
FN=0은 "복사 후 파괴 진행"보다 "전환/보류(redirect/hold)"에서 더 강하게 성립하며, semantics-ar의
FN=0 주장은 ShieldFS/Redemption이 인정한 것과 **동일한 두 한계**(저역치 모방, 모니터가 못 보는
읽기 채널)를 그대로 상속한다.

---

## 3. Q1 — FN=0은 진정으로 달성되었는가

### 3.1 [정정됨] 예산-초과 벌크 공격: fail-closed 차단은 맞음 — 잔여는 "경계 축출"뿐

> **초판 정정.** 초판은 이 경로를 "silent fail-open(공격이 끝까지 진행)"으로 적었다. **오류다.
> 정책은 fail-closed가 맞다** — 예산 소진 시 파괴 쓰기는 **차단**된다(Phase G가 검증). 아래는
> 코드로 재확인한 정확한 동작이며, 잔여는 초판보다 훨씬 좁고 낮은 심각도다.

**시나리오(범위 내, 실현 가능):** ENFORCE. 공격자가 (a) 파일별 키 + 즉시 zeroize로 Oracle
무력화(BlackCat `Zeroize`, Conti 키 소거로 **실제 관측**), (b) 열거 없는 하드코딩 표적으로
Phantom 완전 우회, (c) 예산 초과 규모로 인플레이스 암호화. **즉 유죄판정(conviction) 0회.**

**코드로 본 정확한 동작 (Phase G: cap=1MB, 20×128KB, 유죄 0):**
- 스테이지 레코드는 기본 **probation**(`engine/src/preserve.c:62`), protected 진입은 conviction
  뿐(`sar_preserve_promote`). 유죄 0 → `protected_bytes=0`.
- 동기 인플레이스 경로(`SarPreWrite`→`SarCaptureInPlaceRegion`→`SarPreserveStage`)는 `prot+ct>cap`
  이 `0+128K>1M`=거짓이라 **DROPPED에 도달하지 않고**, 대신 `evict_probation_oldest`로 **가장
  오래된 홀드를 축출**해 자리를 만든 뒤 쓰기를 통과시킨다(`preserve.c:812/168`).
- 용량 차단(`SarPreserveWouldExceed`, **total_bytes** 기준)은 **지연 워커**(`capture.c:337`)와
  mmap-arm(`operations.c:720`)에만 있고 **동기 인플레이스 경로엔 없다**(`operations.c:434-489`은
  이미-차단 검사만).
- 결과: 저장소가 찬 뒤 지연 차단이 실효되면 이후 파일은 `STATUS_ACCESS_DENIED`로 **차단됨**
  (Phase G의 `ovfBlocked>0`가 이것을 검증). **그러나** "참→차단 실효" 창 동안 동기 경로가
  가장 오래된 홀드(예: file 0)를 축출해 최신 홀드(file 8)를 받으므로, 이미 덮어써졌고 키도 없는
  **경계 소수 파일은 복구 불가**가 된다. Phase G 단언은 `ovfBlocked>0`(=공격 정지)일 뿐,
  덮어쓴 집합의 복구 가능성은 **검사하지 않는다.**

**정확한 판정:** "예산 소진 시 무조건 차단"은 **맞다(fail-closed).** 공격은 정지되고 **총 손실은
예산 규모로 유계화**된다 — 초판이 시사한 "머리 전체 손실/무한 손실"은 틀렸다. 남는 것은
**딱 하나**: 지연 차단이 동기 probation 축출과 경합하는 창에서, **유죄 0회일 때만**, 경계에서
축출된 **소수 파일**이 복구 불가일 수 있다는 점(Phase G 규모에선 ~1파일). 이는 유계-저장소의
설계상 잔여(DESIGN_REVIEW §5: "III.5.5 pool/reclamation 모델 유지")이지 fail-open 구멍이 아니다.

**문자적 zero-loss를 원한다면(소유자 결정):** 동기 인플레이스 경로에서 `would_exceed(total)`일 때
**축출 대신 쓰기를 차단(block-before-evict)** 하면 경계 잔여가 사라진다. 대가는 ENFORCE에서
양성 벌크 재작성기를 차단할 수 있다는 것(2-pool probation이 피하려던 FP) — FN/FP 트레이드오프의
명시적 선택 문제로 남는다.

**완화 요인:** 빠른 패밀리는 파일당 ~4KB만 부분 암호화(Splunk SURGe)하므로 같은 예산에 매우
많은 파일이 담겨 경계 잔여의 절대 규모가 작다. 또한 실제 랜섬웨어는 열거·키재사용으로 대개
Oracle/Phantom에 조기 유죄판정되어 promote(축출 면제)+first-instance 차단되므로, 이 잔여는
"유죄 0회"라는 인위적 최악에서만, 그것도 경계 소수에 국한된다.

### 3.2 [정정·유효 갭] 배터리는 AES 전용이 아님 — 그러나 스트림 사이퍼가 런타임 미배선

> **초판 정정.** 초판은 "Oracle이 AES-스케줄 계열에만 대응"인 것처럼 시사했다. **틀렸다.**
> `engine/src/battery.c`는 표준 알고리즘을 광범위하게 구현·단위검증한다. 그러나 코드를 끝까지
> 추적하니 **드라이버 런타임 경로에서 스트림 사이퍼는 실제로 시도되지 않는다** — 설계 목표와
> 런타임 배선 사이의 갭이며, 이것이 정확한 finding이다.

**배터리 구현 폭(설계 목표: 달성).** `sar_convict`(battery.c)는 다음을 시도한다:
- **블록**: AES-128/192/256(ECB/CBC/CTR/CFB/OFB + **XTS**), 3DES, SM4, Camellia, ARIA, SEED.
- **스트림**: ChaCha20 / XChaCha20 / Salsa20 / XSalsa20(HChaCha/HSalsa 서브키 포함), RC4.
- 모두 `tests/test_engine.c`의 `stream_suite`/블록 스위트로 **단위검증됨**(순방향 증명).

**런타임 배선(driver → scan_battery): 블록 O, 스트림 X.** 결정적 코드 경로:
- 드라이버는 `req.candidates = NULL; req.candidate_count = 0`로 두고 4MB 스냅샷만 넘긴다
  (`driver/capture.c:537-539`; snapshot=`SAR_CAPTURE_HEAP_BUDGET`=4MB).
- `sar_capture_run`(capture/src/capture.c)은 candidates를 **그대로 전달**할 뿐 스냅샷에서
  후보를 **추출하지 않는다**.
- 따라서 `sar_convict`의 candidates-루프(블록 single/pair, **유일한 `try_stream` 호출(battery.c:420)**,
  rc4-shortkey)는 count=0이라 **전부 no-op**. 스냅샷을 도는 것은 `scan_battery`뿐이다.
- `scan_battery`(battery.c:354)가 실제 하는 것: **AES 구조 스케줄 스캔(전체 4MB)** +
  RC4 S-box 탐지(첫 64KB) + **첫 64KB 브루트 (P,C)** (AES/3DES/SM4/Camellia/ARIA/SEED/XTS/RC4).
  → **`try_stream`은 `scan_battery`에서 호출되지 않는다.** ChaCha/Salsa/XChaCha/XSalsa는 런타임에
  시도되지 않는다.
- (근본 원인·정정) 스트림 키+nonce의 원시 메모리 **브루트는 O(n²)**로 비현실적이다(64KB에서
  ≈1.7×10¹¹ 연산 ≈ 수십초/프로세스; 비동기여도 CPU 세금이 양성 프로세스마다 발생 → Q2 위반).
  그러나 이는 **잘못된 경로**다. AES가 싼 이유는 브루트가 아니라 **구조 자기식별 스캔**(스케줄
  재귀식)이고, 코드는 **RC4(스트림)에도 S-box 구조 스캔을 이미 한다**(battery.c:360). ChaCha/Salsa
  에도 동형 앵커가 있다: **σ 상수 `"expand 32-byte k"`**(상태행렬 워드0-3, 키=워드4-11,
  nonce/counter=워드12-15). 상태가 상주하면 **O(n) σ-스캔 → 히트당 키+nonce 즉시 추출 → O(1) 검증**
  으로 수 밀리초에 해결된다. 즉 스트림 미배선의 진짜 원인은 **"값싼 σ-스캔 구조 검출기 미구현"**
  이지 "근본적으로 예산 밖"이 아니다(초판의 O(n³)/"감당 불가" 서술은 과했으므로 정정).

**두 번째, 더 미세한 위치-예산 비대칭:** AES는 전체 4MB 구조 스캔을 받지만, **비-AES 블록 키는
첫 64KB(`SAR_SCAN_BRUTE_CAP`) 브루트 창 안에서만** 위치 탐색된다. 64KB 밖의 SM4/Camellia/ARIA/
SEED/3DES 키는 미포착. 즉 블록 사이퍼 커버리지도 AES가 위치 측면에서 유리하다(커버리지 갭이
아니라 예산 갭).

**정확한 결과.** 실제 모집단 대비:
- **블록/AES 계열(Ryuk·Play·Dharma·Nefilim·Royal·Medusa·Phobos·GandCrab-AES·LockBit-AES 빌드,
  Rhysida의 파일데이터 AES-256-CTR)** → 런타임 포착됨(AES는 강, 기타 블록은 64KB 창 내).
- **스트림 계열(REvil Salsa20, Conti·Cuba·Babuk·Maze ChaCha, LockBit XSalsa/ChaCha)** → 배터리는
  있으나 **런타임 미배선으로 Oracle 미포착** → 전량 보존(§3.1)으로 이관.
이는 코드 미구현이 아니라 **배선 갭**이므로 수정 난도가 낮다(권고 §5-3). 그리고 미포착이 곧
데이터 손실은 아니다 — 보존이 받는다. 다만 "정의적·무한 복구" 자산이 스트림 패밀리에서 꺼지므로
무게중심이 유계 보존으로 쏠리고, 이는 §3.1의 실무 관련성을 강화한다.

### 3.3 [이론적·미관측 강화 항목] confident-blind 스킵 + 스트림컨텍스트 teardown

`SarStreamConfidentBlind`(`capture.c:828`)는 해당 스트림에서 **읽기가 관측되지 않았고**
쓰기 의도로만 열린 핸들의 인플레이스 보존을 **건너뛴다**(`operations.c:481`, `SarSubmitWrite`도
`operations.c:212`에서 조기 반환). 정상적으로는 스트림 컨텍스트가 FCB에 공유되어 A핸들의
읽기(`READ_OBSERVED`)가 B핸들 쓰기에도 보이므로 우회 불가다. 그러나 **읽기 후 A를 닫아
스트림 컨텍스트가 소멸된 뒤 새 핸들 B로 blind overwrite**하면, B는 fresh 컨텍스트라
`READ_OBSERVED` 미설정 → 보존 스킵, 동시에 Oracle의 P-샘플(`SarSubmitWrite`가 참조하는
`sc->read_sample`, `operations.c:239-244`)도 소멸 → 키 미포착. **원리상 단일·예산내 FN 후보.**

단, **관측된 어떤 패밀리도 이 패턴을 쓰지 않는다** — 전부 단일 OPEN_EXISTING 핸들에서
read+write(교차검증: Conti 누출 소스 `GENERIC_READ|GENERIC_WRITE, OPEN_EXISTING`; BlackCat
`0xC0000000, 0x3`; LockBit/REvil/Ryuk/GandCrab/Medusa 동일). 따라서 이는 **잠복 강화
항목**이지 활성 갭이 아니다. 스트림 컨텍스트 수명(teardown 타이밍)과 프로세스 단위 P-링
(Constitution IV.1.3의 "per-process ring"이 인플레이스 경로에 실장되어 있는지)을 동적으로
검증할 것을 권고한다.

### 3.4 [철회·재분류] 구 "헌장-코드 불일치"는 finding 아님

초판은 아래 둘을 거버넌스 finding으로 적었으나, **헌장 III.5/Part IV가 레거시**(폐기된
whole-file-at-open)이고 재작성 예정(HANDOFF §7.2)이므로 **불일치가 아니라 문서 갱신 대기**다.
finding에서 철회한다. 순수 **코드 사실** 한 건만 §4로 이관한다:
- 용량 차단이 **total_bytes** 기준(`preserve.c:939`)이라 ENFORCE에서 예산 초과 양성 벌크
  재작성기를 차단할 수 있음 → §3.1의 block-before-evict 및 §4.2-②와 **동일한 FN/FP 결정 사안**
  (레거시 헌장 대조가 아니라 채택 설계 기준 실관측).
- 절단-생성(supersede/overwrite create)은 코드가 정상 포착(`operations.c:314 → capture.c:1149`),
  DESIGN_REVIEW §3.4와 일치 — **갭 아님.**

### 3.5 [주요 벡터·정상 처리 확인] 메모리 맵 암호화 — 갭 아님, 그러나 비용 지점

외부 조사에서 **mmap 암호화는 드문 예외가 아니라 주요 벡터**임이 확인됐다: **Babuk(누출 소스
`main.cpp`: `CreateFileW(OPEN_EXISTING, GENERIC_READ|GENERIC_WRITE)` → `CreateFileMappingA(PAGE_READWRITE)`
→ `MapViewOfFile` → ChaCha 인메모리 ×2, ReadFile/WriteFile·SetFilePointer 미사용)**, **Maze(McAfee
디셈블: "all changes are applied in the mapping directly... without using a file pointer" — 명시적
anti-forensic)**, 그리고 WastedLocker·LockFile·NotPetya. 이들은 콘텐츠 쓰기가 System(PID 4)의
paging I/O로 나오므로 순수 read/write-IRP 모니터를 우회한다.

semantics-ar의 대응은 **쓰기 가능 매핑은 반드시 쓰기 가능 open을 선행한다**는 사실을 이용해
`IRP_MJ_ACQUIRE_FOR_SECTION_SYNCHRONIZATION`(RW)에서 포착하는 것이다
(`operations.c:672 SarPreAcquireForSection` → `SarMmapArm`/`SarSubmitMetadata(SECTION_SYNC)`;
매핑이 더럽혀지기 전, `SECTION_DIRTY` 설정으로 confident-blind도 무효화). 이는 프론티어 권고
(Sophos·RansomGuard 블로그: "섹션 생성 감시")와 **일치하며 갭이 아니다 — 강점이다.** 다만 두 가지를
동적 검증할 것: (i) 섹션-arm 시점에 **어느 페이지가 더럽혀질지 모르므로**, 전 매핑 영역을 eager
복사하는지 / dirty-page 추적인지 — 전자면 Babuk의 최대 40MB 전량 매핑에서 큰 동기 복사 비용(§4.2-①),
(ii) 용량 차단(`operations.c:720`)이 이 경로에서 §3.1과 동일한 "지연차단 vs 동기축출" 경합을 갖는지.

> 정리: **확인된 실재 FN은 없다.** §3.1은 정정 후 "경계 축출 잔여"(fail-closed로 공격은 정지,
> 유죄0회 시 경계 소수만 복구불가·유계·저심각)로 좁혀졌고 공시된 유계-저장소 설계 내부다.
> §3.3은 미관측 이론 후보, §3.2는 정직성/altitude, §3.4는 철회(레거시 문서), §3.5는 강점+효율비용.
> **와이퍼·mmap·절단-생성·커널 공격자는 유효 반례가 아니다**(각각 범위 밖·섹션-arm에서
> 포착·PreCreate 포착·IX.1).

---

## 4. Q2 — FP 최소화/자원 효율은 최적에 닿아 있는가

### 4.1 프론티어 대비 우위 (검증됨)

- **게이트 신호가 프론티어보다 원리적으로 우월.** diff-제한 조건부 novelty는 (a) 고엔트로피
  원본(압축/미디어)에서의 FP를 구조적으로 회피하고, (b) **부분/간헐 암호화**(256B 블록 내
  16B 스트라이프)를 포착한다. 외부 조사상 부분 암호화는 사실상 표준이 됨(LockBit 4KB, BlackCat
  패턴, Play 1MB 청크, Conti header-only, Black Basta 64/192). 엔트로피 델타(RansomGuard·
  CryptoDrop·Redemption/ShieldFS 피처)는 이 둘에서 실패한다. **실측 가능한 우위.**
- **보존 I/O 효율이 프론티어보다 높음.** 인플레이스는 **기록된 영역만** 보존, 삭제/rename엔
  **하드링크**(거의 무복사). ShieldFS/RansomGuard의 전체-파일 섀도보다 저렴하다.
- **once-per-process Oracle + off-IRP 지연 스냅샷** — PayBreak식 크립토-콜당 후킹 비용 회피.
- **2-pool probation** — 양성 벌크 재작성기를 차단하지 않으려는 FP 회피 설계(RansomGuard의
  6회-종료가 정확히 이 FP를 유발하는 것과 대비).

### 4.2 최적성을 깨는 구체 지점 (개선 여지)

1. **모든 "영역 첫 파괴 쓰기"에 대한 eager 동기 보존 — T-게이트와 무관.** T는 지연 워커에서
   Oracle만 게이트하고(`capture.c:468`), 보존은 pre-write에서 무조건 선행된다. 이는 구조적
   필연이다(파괴 전에 복사해야 하고, 비동기 novelty 판정을 기다릴 수 없다) — ShieldFS/Redemption도
   동일 비용을 낸다. 그러나 결과적으로 **모든 양성 인플레이스 편집이 data-scan 섹션 생성 + 읽기 +
   AES 암호화-저장 + write-through 디스크를 쓰기 통과 전에 동기적으로 지불**한다. 다수의 작은
   인플레이스 편집 워크로드(DB·IDE·빌드·VM 디스크)에서 이는 사용자 체감 지연이 될 수 있다.
   프론티어 수치(Redemption 2.6%, ShieldFS 최초-백업 마이크로벤치 1.8–3.8×)는 체감 수준에선
   견딜 만하나 쓰기 마이크로벤치에서 비용이 큼을 시사한다. **"사용자가 인지 못 함"의 진위는
   전적으로 write-through 암호화 저장소 처리율에 달려 있고, 이는 X.2가 스스로 미측정 배포
   특성으로 남겨둔 값**이다. → 대표 워크로드에서의 실측이 이 주장의 유일한 검증 수단이다.
2. **total-vs-protected 차단(§3.1과 동일 사안)**: 용량 차단이 total_bytes 기준이라 ENFORCE에서
   양성 벌크 재작성기를 예산 초과 시 차단할 수 있는 FP. §3.1의 block-before-evict를 채택하면 이
   FP가 더 강해지고, 반대로 probation을 차단에서 빼면 §3.1 경계 잔여가 커진다 — **하나의 FN/FP
   축**이다. 채택 설계에서 명시적 결정 필요(현재 코드는 total 기준 = FN 우선).
3. **supersede/overwrite-create의 전체-파일 동기 복사**(`SarCaptureWholeContent`): 대용량 파일
   supersede 시 pre-create에서 전체 복사. 양성 경로에선 드물어 수용 가능하나 상한 필요.

**Q2 판정:** FN=0 제약 하에서 **거의 최적**이다. 탐지 신호는 프론티어를 앞서고, 보존은
프론티어보다 I/O 효율적이다. 환원 불가능한 비용(eager 동기 보존)은 프론티어와 공유하는
근본 비용이다. 명확한 개선 기회는 (i) total/protected 차단 회계 일관화로 ENFORCE FP 회복,
(ii) 대표 워크로드에서 write-through 저장소 처리율 실측으로 "무인지" 주장 실증 — 둘뿐이다.

---

## 5. 권고 (actionable)

1. **§3.1 경계 잔여 제거(소유자 결정)**: 문자적 zero-loss가 목표라면, 인플레이스 pre-write
   동기 경로에 `would_exceed(total)` 검사를 **축출 이전에** 두어 **block-before-evict**로 전환
   → 경계 축출 잔여 제거. 대가는 ENFORCE 양성 벌크 재작성기 FP(2-pool probation과의 긴장) —
   FN/FP 트레이드오프의 명시적 선택이며, Phase G를 "차단됨>0"이 아니라 "덮어쓴 집합 100% 복구"로
   강화해 회귀 방지.
2. **주장 언어 정합화**: "Oracle·Phantom 완전 우회 시에도 무조건 FN=0" 대신 **"보존 예산 내부
   FN=0 / 예산 초과 시 fail-closed 차단으로 손실을 예산 규모로 유계화 / 예산은 사용자 소유 자원"**
   — HANDOFF의 "fail-closed at capacity" 및 프론티어(ShieldFS/Redemption)의 정직한 경계와 일치.
3. **§3.2 스트림 σ-스캔 구조 검출기 실장(올바른·값싼 경로)**: 배터리엔 ChaCha/Salsa가 구현·
   단위검증돼 있으나 런타임(`scan_battery`)에서 시도되지 않는다. **O(n²) 브루트나 미구현
   후보-추출기가 아니라**, `aes_schedule_scan`/RC4 S-box 스캔과 동형으로 **σ 상수
   `"expand 32-byte k"`를 O(n)로 스캔**해 히트당 상태행렬에서 키+nonce를 추출→`try_chacha`/
   `try_salsa` 1회 검증하는 **σ-스캔 검출기를 `scan_battery`에 추가**하면 값싸게(수 ms) 닫힌다.
   상태 비상주·zeroize·변형상수 구현은 IX.2로 위임(AES 즉시-zeroize와 동급). 아울러 비-AES 블록의
   64KB 브루트 창(`SAR_SCAN_BRUTE_CAP`) 전략 재고. `test_engine.c` 스트림 스위트를 드라이버 경로
   (candidates=NULL + scan_buffer)로 재현하는 회귀 테스트로 승격. REvil/Conti/Babuk/Maze/LockBit-
   스트림이 이 경로라 우선순위 높음.
4. **§3.3 동적 검증**: 읽기-후-close-후-blind-overwrite 시퀀스에 대해 스트림 컨텍스트 수명과
   프로세스 단위 P-링(HANDOFF §7.1 게이트/보존 완결성 감사 항목)을 하네스로 검증.
5. **헌장 재작성(이미 HANDOFF §7.2 예정)**: III.5/Part IV는 폐기된 whole-file-at-open 설계를
   서술하므로 채택 설계(DESIGN_REVIEW)로 재작성 — 별도 신규 finding이 아니라 기존 예정 작업.
6. **§4.2-① 실증**: 대표 쓰기 집약 워크로드에서 보존 저장소 write-through 처리율/지연을 측정,
   "무인지" 주장의 유일한 객관 근거로 삼는다.

---

### 근거 출처 (외부 1차 자료, 발췌)
- RansomGuard 소스: github.com/0mWindyBug/RansomGuard (config.h, evaluate.cpp, filters.cpp).
- Redemption(RAID'17) seclab.nu; ShieldFS(ACSAC'16) conand.me/necst; PayBreak(AsiaCCS'17) seclab.bu.edu;
  CryptoDrop(ICDCS'16) cise.ufl.edu; UNVEIL(USENIX'16); RWGuard(RAID'18).
- 랜섬웨어 파일 I/O 1차 RE: Conti 누출 소스(gharty03/locker.cpp), BlackCat(SecurityScorecard),
  LockBit(Lexfo/chuongdong/calif.io), REvil(amossys), Ryuk(n1ghtw0lf), GandCrab(cyber.wtf),
  Nefilim/Cuba/Play/Royal/Medusa/Rhysida/Phobos(각 벤더 RE), mmap 계열(Sophos LockFile/WastedLocker,
  CrowdStrike NotPetya), 속도(Splunk SURGe).
- MS 문서: CreateFile/NtCreateFile disposition(FILE_SUPERSEDE/OVERWRITE_IF 절단), IRP_MJ_CREATE.
