#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

if [ "$#" -ne 1 ]; then
    echo "cleanup.sh <NUMBER_OF_SERVERS>"
    exit 1
fi

$DIR/cleanup-servers.py
$DIR/cleanup-infra.sh $1
