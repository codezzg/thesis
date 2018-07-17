: ${COMPILER:=/usr/local/src/VulkanSDK/1.1.73.0/x86_64/bin/glslangValidator}


resolve_includes() {
	shaderCode="$1"
	baseDir="$2"
	recursion=${3:-0}

	if (( recursion > 10 )); then
		echo "[ ERR ] Recursion limit exceeded." >&2
		return 1
	fi

	newCode="$(echo "$shaderCode" | awk -v"DIR=$baseDir" '
/^#pragma include/ {
	if (NF < 3) {
		print FILENAME ":" NR ": expected filename after #pragma include" >/dev/stderr
		exit 1;
	}

	includedFile = $3;
	if (!(includedFile in includedSoFar)) {
		system("cat " DIR "/" includedFile);
		includedSoFar[includedFile] = 1;
	}
	next;
}

{
	print;
}
')"
	[[ $? != 0 ]] && {
		echo "There were errors: exiting." >&2
		return 1
	}

	if [[ $(echo "$newCode" | grep -c '#pragma include') > 0 ]]; then
		newCode="$(resolve_includes "$newCode" "$baseDir" $((recursion + 1)))"
	fi

	echo "$newCode"
}

compile() {
	shaderFile="$1"
	baseDir="$(dirname $shaderFile)"
	case "$shaderFile" in
	*.vert|*.vs) stage=vert; ;;
	*.frag|*.fs) stage=frag; ;;
	*.geom|*.gs) stage=geom; ;;
	*.tesc) stage=tesc; ;;
	*.tese) stage=tese; ;;
	*.comp) stage=comp; ;;
	*) echo "Invalid extension: $shaderFile."; exit; ;;
	esac

	shaderCode="$(< $shaderFile)"
	shaderCode="$(resolve_includes "$shaderCode" "$baseDir")"

	[[ $verbose == 1 ]] && echo -e "--Shader type: $stage; code:\n$shaderCode\n----------"
	echo "$shaderCode" | "$COMPILER" -V --stdin -o "$shaderFile.spv" -S $stage > >(egrep -v '^stdin$')
	return $?
}


while [[ $# > 0 ]]; do
	case $1 in
	-v) verbose=1; shift; ;;
	-c) shift; COMPILER="$1"; shift; ;;
	*) echo "Usage: $0 [-v] [-c COMPILER]" >&2; exit 1; ;;
	esac
done

for sh in shaders/*.{vert,frag}; do
	[[ $verbose == 1 ]] && echo "Compiling $sh"
	compile $sh
	if [[ $? == 0 ]]; then
		echo "[ OK ] Compiled $sh."
	else
		echo "[ ERR ] Error compiling $sh."
	fi
done
