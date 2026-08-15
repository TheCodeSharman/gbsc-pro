{
  description = "GBSC Pro — native Linux flasher, AV-module protocol, and diagnostics (fork of RetroScaler/gbsc-pro)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "aarch64-darwin" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      devShells = forAllSystems (pkgs:
        let
          # ymodem isn't in nixpkgs; pin upstream inline (used by the AV-module flasher).
          ymodem = pkgs.python3Packages.buildPythonPackage {
            pname = "ymodem";
            version = "1.5";
            pyproject = true;
            src = pkgs.fetchFromGitHub {
              owner = "alexwoo1900";
              repo = "ymodem";
              rev = "1d9611bb5d1b4c01149b228aeee9893588d424ef";
              hash = "sha256-UeGF/qbEIwnHpXfouCXwAv19pNOqXJlmqNfsdK7Iz90=";
            };
            build-system = [ pkgs.python3Packages.setuptools ];
            dependencies = with pkgs.python3Packages; [ ordered-set pyserial ];
          };
          # pyserial: AV-module flasher; ymodem: YMODEM transfer; websocket-client:
          # read gbs-control's live status/terminal over ws://<ip>:81/; pytest:
          # runs tools/gbsc-pro-hwtest against a live unit.
          pythonEnv = pkgs.python3.withPackages (ps: [ ps.pyserial ymodem ps.websocket-client ps.pytest ]);
        in
        {
          default = pkgs.mkShell {
            packages = [
              pythonEnv
              pkgs.esptool       # flash / dump the ESP8266 half
              pkgs.arduino-cli   # firmware build (build/Makefile drives it)
              pkgs.gnumake

              # The web UI build chain: public/src/index.ts -> index.js -> webui.html
              # -> webui_html.h, driven by `npm run build` in public/. Without these
              # the chain cannot run at all, which is how the /spiffs/ -> /fs/ rename
              # nearly shipped a UI still calling the old routes: three of the four
              # files are checked-in build artefacts, so hand-editing whichever one
              # you found looked like it worked. See CLAUDE.md, "The web UI is
              # generated four times over".
              #
              # nodejs brings npm, and `npm ci` in public/ installs the tsc the repo
              # pins -- 4.1.3, per package-lock.json. Deliberately NOT pkgs.typescript:
              # that is 5.9.3, which rejects this source outright
              # (index.ts:35, TS2322 on Uint8Array vs ArrayBuffer). A missing tsc is a
              # better failure than one that type-errors on code that compiles fine
              # with the pinned version.
              pkgs.nodejs           # node runs public/scripts/build.js; npm supplies tsc
              pkgs.xxd              # public/scripts/html2h.sh pipes gzip through it
            ];
            shellHook = ''
              echo "gbsc-pro dev shell — python3 (pyserial, ymodem, websocket-client, pytest), esptool, arduino-cli + make, node/tsc (web UI)"
              echo "  firmware: make -C build setup   (once)   then   make -C build"
              echo "  web UI:   cd 'GBSC-Pro-Source code/gbs-control/public' && npm run build"
              echo "  hardware: pytest --host=gbscontrol.local   (needs a running unit)"
            '';
          };
        });
    };
}
