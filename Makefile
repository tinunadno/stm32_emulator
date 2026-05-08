.PHONY: up down build logs test test-hello

up:
	docker compose up --build -d

down:
	docker compose down

logs:
	docker compose logs -f sim-service

build:
	$(MAKE) -C src

test:
	bash scripts/test_job.sh

test-hello:
	$(MAKE) -C examples/hello_world
	bash scripts/test_job.sh examples/hello_world/hello_world.bin
