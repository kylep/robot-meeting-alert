# robot-meeting-alert

## Git workflow

1. Commit straight to `main`. No branches or PRs needed.
2. Scan for secrets before every commit. Review the staged diff
   (`git diff --cached`) and run a scanner if one is available
   (e.g. `gitleaks protect --staged`). Never commit tokens, API keys,
   passwords, `.env` files, or credentials. Stage files individually,
   never `git add -A`.
3. Push after every commit (`git push origin main`).
