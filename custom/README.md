# codeyoma fork 운영 메모

이 fork는 upstream [Fincept-Corporation/FinceptTerminal](https://github.com/Fincept-Corporation/FinceptTerminal)에 KRX 4분 예측 엔진(비공개 저장소 `krx-quant`)을 보는 화면 하나를 더한 개인용 빌드다. 엔진과 알고리즘은 이 저장소에 두지 않는다.

## 브랜치

| 브랜치 | 용도 | 규칙 |
|---|---|---|
| `main` | upstream 미러 | 직접 커밋하지 않는다. 매주 workflow가 upstream으로 fast-forward 한다. |
| `custom` | 개인 빌드 (기본 브랜치) | 모든 개인 작업은 여기. upstream은 **릴리스 태그만** merge 한다. rebase 하지 않는다. |

`custom/UPSTREAM_BASE`에는 `custom`이 기반으로 하는 upstream 릴리스 태그를 적는다.

## upstream 릴리스 반영

새 릴리스가 나오면 `custom-upstream-watch` workflow가 issue를 연다. 반영 절차:

```bash
git checkout custom
custom/merge-upstream-release.sh
```

스크립트는 최신 upstream 릴리스 태그를 merge 하고 `custom/UPSTREAM_BASE`를 갱신한다. 충돌이 나면 멈추고 마무리 방법을 출력한다. Windows에서는 Git Bash에서 실행한다.

## main 자동 동기화와 SYNC_TOKEN

workflow는 기본 `GITHUB_TOKEN`으로 `main`을 동기화한다. upstream이 `.github/workflows/` 파일을 바꾼 주에는 이 토큰으로 동기화할 수 없어서 실패 issue가 열린다. 그때는 저장소 페이지의 **Sync fork** 버튼을 누르면 된다. 자동으로 처리하려면 fine-grained PAT(이 저장소만, Contents와 Workflows 읽기·쓰기)를 만들어 `SYNC_TOKEN` secret으로 등록한다.

## upstream 파일 수정 원칙

- 새 코드는 새 디렉터리에 둔다 (`fincept-qt/src/screens/krx_quant/`, `fincept-qt/src/services/krx_quant/`).
- 화면 등록 때문에 고치는 upstream 파일은 아래 목록으로 제한하고, 고칠 때마다 이 표를 갱신한다.
- Fincept SQLite migration(`src/storage/sqlite/migrations/`)은 쓰지 않는다. 데이터는 엔진이 가진다.
- 개인 빌드는 앱 내 자동 업데이트를 끈다. 공식 버전으로 덮어써지는 것을 막기 위해서다.

| upstream 파일 | 수정 내용 |
|---|---|
| (P3에서 채움) | |

## upstream workflow

이 fork에서는 upstream의 예약·릴리스 workflow를 끈다(GitHub Actions 설정에서 disable). 개인 빌드용 workflow는 P3에서 `custom-` 접두사로 추가한다.
