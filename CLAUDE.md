# robot-meeting-alert

## Git workflow

1. Commit straight to `main`. No branches or PRs needed.
2. Scan for secrets before every commit. Review the staged diff
   (`git diff --cached`) and run a scanner if one is available
   (e.g. `gitleaks protect --staged`). Never commit tokens, API keys,
   passwords, `.env` files, or credentials. Stage files individually,
   never `git add -A`.
3. Push after every commit (`git push origin main`).

## Layout

Each teaching project lives in its own folder (`project-1/`, `project-2/`, …)
with its own Makefile, firmware, Mac app, tests and docs. Run `make` targets
from inside the project folder. Shared docs (kit inventory) live in `docs/`.
