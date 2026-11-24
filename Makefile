# Version from file
VERSION := $(shell cat version.txt)

## Check the core firmware submodule is pinned to its $(VERSION) tag
.PHONY: check-core-pin
check-core-pin:
	./check_core_pin.sh

## Tag this version
.PHONY: tag
tag: check-core-pin
	git tag $(VERSION) && git push origin $(VERSION) && \
	echo "Tagged: $(VERSION)"
