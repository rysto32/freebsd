#!/bin/sh

unset target input

while getopts "i:o:" opt
do
	case "$opt" in
	i)
		input=$OPTARG
		;;
	o)
		target=$OPTARG
		;;
	*)
		echo "Unrecognized argument '-%opt'" >&2
		exit 1
	esac
done

shift $(($OPTIND - 1))

if [ -n "$1" ]
then
        echo "Unrecognized argument $1" >&2
        exit 1
fi

if [ -z "$target"]
then
	echo "-o option is mandatory" >&2
	exit 1
fi

if [ -z "$input"]
then
	echo "-i option is mandatory" >&2
	exit 1
fi

${NM} -n --defined-only $input | while read offset dummy what
do
	echo "#define	${what}	0x${offset}"
done > ${target}
