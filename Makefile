PYTHON ?= python3

.PHONY: help clean clean-dry-run

help:
	@echo "make clean          Remove generated builds, build environments and caches"
	@echo "make clean-dry-run  List those paths without removing them"

clean:
	@$(PYTHON) tools/clean_builds.py

clean-dry-run:
	@$(PYTHON) tools/clean_builds.py --dry-run

.PHONY: release-plan release-build release-check release-prepare release-status source-dry-run
release-plan:
	@$(PYTHON) scripts/release.py plan
release-build:
	@$(PYTHON) scripts/release.py build
release-check:
	@$(PYTHON) scripts/release.py check
release-prepare:
	@$(PYTHON) scripts/release.py prepare
release-status:
	@$(PYTHON) scripts/release.py status
source-dry-run:
	@$(PYTHON) scripts/release.py source --to gitlab,github --dry-run
