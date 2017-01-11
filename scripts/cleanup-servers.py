#!/usr/bin/env python

import boto3
import subprocess

client = boto3.client('ecs')
cluster_name = 'liblogcabin-ecs-demo-cluster'

response = client.list_tasks(cluster=cluster_name)

for arn in response['taskArns']:
    client.stop_task(cluster=cluster_name, task=arn)
