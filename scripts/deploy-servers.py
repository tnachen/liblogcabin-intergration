#!/usr/bin/env python

import argparse
import boto3
import json
import sys
import subprocess
import os

parser = argparse.ArgumentParser(description='Launch raft-example-servers in AWS.')
parser.add_argument('count', help="Number of servers to launch", type=int)
args = parser.parse_args()

with open(os.path.dirname(os.path.abspath(__file__)) + '/data/task-template.json', 'r') as task_template_file:
    task_template=task_template_file.read().replace('\n', '')

base_http_port = 12301
base_raft_port = 6001
cluster_name = "liblogcabin-ecs-demo-cluster"
client = boto3.client('ecs')

response = client.list_container_instances(cluster=cluster_name)
instance_arns = response['containerInstanceArns']

response = client.describe_container_instances(
    cluster=cluster_name,
    containerInstances=instance_arns)
container_instances=response['containerInstances']

ec2Ids = []
for detail in container_instances:
    ec2Ids.append(detail['ec2InstanceId'])

if len(instance_arns) != args.count:
    print("Expecting {} instances launched, found {}", args.count, len(instance_arns))
    sys.exit(1)

if len(container_instances) != args.count:
    print("Expecting {} container instances, found {}", args.count, len(container_instances))
    sys.exit(1)

ec2_client = boto3.client('ec2')
response = ec2_client.describe_instances(
    InstanceIds=ec2Ids)
details = []
for reservation in response['Reservations']:
    for instance in reservation['Instances']:
        details.append(instance)

instance_details = []
for container_instance in container_instances:
    for detail in details:
        if detail['InstanceId'] == container_instance['ec2InstanceId']:
            instance_details.append(detail)
            break

leader_address = ""
for i in range(args.count):
    if i != 0:
        leader_address = instance_details[0]['PrivateIpAddress'] + ":" + str(base_http_port)

    task_def = task_template \
        .replace("$ID", str(i)) \
        .replace("$HTTP_PORT", str(base_http_port + i)) \
        .replace("$RAFT_ADDRESS", instance_details[i]['PrivateIpAddress'] + ":" + str(base_raft_port + i)) \
        .replace("$LEADER_ADDRESS", leader_address)

    response = client.register_task_definition(
        **json.loads(task_def)
    )

    response = client.start_task(
        cluster=cluster_name,
        taskDefinition="raft-example-server-" + str(i),
        containerInstances=[instance_arns[i]])

    task_arn = response['tasks'][0]['taskArn']

    if i == 0:
        # Wait until leader is running
        waiter = client.get_waiter('tasks_running')
        waiter.wait(cluster=cluster_name, tasks=[task_arn])
        print("Leader is running")


print("Servers tasks all started, check ECS console to see when tasks are all running!")
