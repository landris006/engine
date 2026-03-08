{
  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = {
    nixpkgs,
    flake-utils,
    ...
  }:
    flake-utils.lib.eachDefaultSystem
    (
      system: let
        pkgs = import nixpkgs {inherit system;};

        vulkan-sdk = pkgs.symlinkJoin {
          name = "vulkan-sdk";
          paths = with pkgs; [
            vulkan-headers
            vulkan-loader
            vulkan-tools
            vulkan-validation-layers
            shaderc
            shaderc.static
            glslang
          ];
        };
      in
        with pkgs; {
          devShells.default =
            mkShell.override {stdenv = gcc14Stdenv;}
            rec {
              buildInputs = [
                pkg-config
                gcc14
                clang-tools
                tbb

                shader-slang

                cmake
                neocmakelsp
                cmake-lint

                gdb

                renderdoc

                vulkan-sdk
                vulkan-utility-libraries
                shaderc

                glfw
                glm

                libxkbcommon
                libGL

                wayland

                libxcursor
                libxrandr
                libxi
                libx11
                libxxf86vm
                libxinerama
                libxcb
              ];

              VULKAN_SDK = "${vulkan-sdk}";
              VK_LAYER_PATH = "${vulkan-validation-layers}/share/vulkan/explicit_layer.d";
              LD_LIBRARY_PATH = "${lib.makeLibraryPath buildInputs}";
            };
        }
    );
}
