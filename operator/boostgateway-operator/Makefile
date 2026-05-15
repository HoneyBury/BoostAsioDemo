IMG ?= ghcr.io/honeybury/boostgateway-operator:v0.1.0

.PHONY: build
build:
	go build ./...

.PHONY: run
run:
	go run ./main.go

.PHONY: docker-build
docker-build:
	docker build -t $(IMG) .

.PHONY: install
install:
	kubectl apply -f config/crd/bases/gateway.boost.io_boostgatewayclusters.yaml
	kubectl apply -k config/default

