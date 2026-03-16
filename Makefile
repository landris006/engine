.PHONY: build run headless sync

build:
	cmake --build build/debug -j

run: build
	build/debug/engine

headless: build
	build/debug/engine_headless $(ARGS)

sync:
	rsync -av --exclude='build' /home/andris/src/engine/ home:/home/andris/src/engine/

run-test: build
	build/debug/engine_headless --model ~/Downloads/Bistro_v5_2/bistro_exterior_out/bistro_exterior.gltf --samples 1000

run-remote: sync
	ssh home 'cd /home/andris/src/engine && nix develop --command make run-test' \
		&& scp home:/home/andris/src/engine/output.png . \
		&& eog output.png
