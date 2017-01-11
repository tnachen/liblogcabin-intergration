#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

if [ "$#" -ne 1 ]; then
    echo "deploy.sh <NUMBER_OF_SERVERS>"
    exit 1
fi

echo "Deploying EC2 infra.."
$DIR/setup-infra.sh $1
echo "Deploying servers with ECS.."
$DIR/deploy-servers.py $1
