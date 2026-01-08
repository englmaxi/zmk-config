workdir-name := 'zmk-workspace'

workdir := absolute_path(workdir-name)
outdir := absolute_path('out')
moduledir := absolute_path('.')
configdir := absolute_path('config')

default:
    @echo "Use 'nix develop', then run 'just init'"
    @echo "List all build targets with 'just list'"
    @echo "Build using 'just build TARGET'"
    @echo "Clean the build folder via 'just clean'"
    @echo "Delete the build environment via 'just clean-all'"

list filter="all":
    @just _parse {{filter}} | awk -F',' '{print $NF}' | sort

init:
    @just _init-workspace
    west init -l {{ workdir }}/zmk/app/
    (cd {{workdir}}/zmk/app && west update)
    (cd {{workdir}}/zmk/app && west zephyr-export)

build name *west_args:
    #!/usr/bin/env bash
    mkdir -p {{outdir}}
    target="$(just _parse {{name}} | awk 'NR==1')"
    [[ -z $target ]] && echo "Target not found..." >&2 && exit 1
    IFS=, read -r board shield snippet cmake_args artifact <<< "$target"
    just _build "$board" "$shield" "$snippet" "$cmake_args" "$artifact" {{west_args}}

build-all name *west_args:
    #!/usr/bin/env bash
    mkdir -p {{outdir}}
    targets="$(just _parse {{name}})"
    [[ -z $targets ]] && echo "Targets not found..." >&2 && exit 1
    echo "$targets" | while IFS=, read -r board shield snippet cmake_args artifact; do
        just _build "$board" "$shield" "$snippet" "$cmake_args" "$artifact" {{west_args}}
    done

clean:
    rm -rf {{outdir}}
    rm -rf {{workdir}}/zmk/app/build

clean-all:
    @just clean
    rm -rf {{workdir}}


### helpers ###

_init-workspace:
    #!/usr/bin/env bash
    mkdir -p {{workdir}}
    just _west-projects | while read -r name remote rev; do \
        just _clone "$name" "$remote" "$rev"; \
    done

_parse filter="all":
    #!/usr/bin/env bash
    attrs="board, shield, snippet, \"cmake-args\", \"artifact-name\": (.\"artifact-name\" // (.board + \"_\" + .shield))"
    filter="if \"{{filter}}\" == \"all\" then . else select(.\"artifact-name\" | contains(\"{{filter}}\")) end"
    format="join(\",\")"
    yq -r ".include[] | { $attrs } | $filter | $format" build.yaml

_west-projects:
    #!/usr/bin/env bash
    attrs="name, remote, revision"
    format="join(\" \")"
    yq -r ".manifest.projects[] | { $attrs } | $format" ./config/west.yml

_west-remotes:
    #!/usr/bin/env bash
    attrs="name, \"url-base\""
    format="join(\" \")"
    yq -r ".manifest.remotes[] | { $attrs } | $format" ./config/west.yml

_clone name remote rev:
    #!/usr/bin/env bash
    base=$(just _west-remotes | awk -v r="{{remote}}" '$1==r {print $2}')
    dir="{{workdir}}/{{name}}"

    if [ -d "$dir/.git" ]; then
        echo "{{name}} already exists"
    else
        git clone --branch "{{rev}}" "$base/{{name}}.git" "$dir"
    fi

_build board shield snippet cmake_args artifact *west_args:
    @echo "Building {{artifact}} ..."
    (cd {{workdir}}/zmk/app && \
     west build \
        {{west_args}} \
        -d build/{{artifact}} \
        -b "{{board}}" \
        -S "{{snippet}}" -- \
        -DZMK_CONFIG="{{configdir}}" \
        -DSHIELD="{{shield}}" \
        -DZMK_EXTRA_MODULES="$(find {{workdir}} -maxdepth 1 -type d ! -name 'zmk' ! -name {{workdir-name}} | paste -sd ';');{{moduledir}}" \
        {{cmake_args}} \
    )
    @cp --verbose {{workdir}}/zmk/app/build/{{artifact}}/zephyr/zmk.uf2 {{outdir}}/{{artifact}}.uf2

