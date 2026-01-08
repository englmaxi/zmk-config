{
  description = "ZMK dev shell";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    systems.url = "github:nix-systems/default-linux";

    zephyr.url = "github:zmkfirmware/zephyr/v4.1.0+zmk-fixes";
    zephyr.flake = false;

    zephyr-nix.url = "github:nix-community/zephyr-nix";
    zephyr-nix.inputs.nixpkgs.follows = "nixpkgs";
    zephyr-nix.inputs.zephyr.follows = "zephyr";
  };

  outputs = {
    nixpkgs,
    systems,
    ...
  } @ inputs: let
    inherit (nixpkgs) lib;
    forAllSystems = lib.genAttrs (import systems);
  in {
    devShells = forAllSystems (
      system: let
        pkgs = nixpkgs.legacyPackages.${system};
        zephyr = inputs.zephyr-nix.packages.${system};
      in {
        default = pkgs.mkShell {
          packages = [
            (zephyr.sdk-0_16.override {
              targets = [
                "arm-zephyr-eabi"
              ];
            })
            zephyr.pythonEnv
            zephyr.hosttools
            pkgs.cmake
            pkgs.ninja
            pkgs.protobuf
            pkgs.python313Packages.grpcio-tools

            pkgs.just
            pkgs.yq
          ];
        };
      }
    );
  };
}
