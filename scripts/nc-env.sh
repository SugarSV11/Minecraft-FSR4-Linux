# source scripts/nc-env.sh : exports NC_NEURAL / NC_EVID_<n> helpers with the same resolution as tools/ai/ncpaths.py (verified copy on the Steam disk, else the old ~/Escritorio path).
NC_STEAM=/run/media/system/steam/NeuralCraft-Data
nc_path() { local name="$1" sub="$2"; if [ -n "${NC_DATA_ROOT:-}" ]; then echo "$NC_DATA_ROOT/$sub/$name"; elif [ -e "$NC_STEAM/.verified-$name" ]; then echo "$NC_STEAM/$sub/$name"; else echo "$HOME/Escritorio/$name"; fi; }
nc_evid() { nc_path "FSR4-evidence-session$1" evidence; }
NC_NEURAL="$(nc_path FSR4-neural-data datasets)"
