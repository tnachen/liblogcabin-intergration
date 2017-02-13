This repo contains a sample server utilizing liblogcabin's raft library, also scripts to deploy and test a cluster in AWS.

== Prerequites ==
Install jq through OS package manager.
Install python, boto3 and awscli python libraries.

Run aws configure to configure AWS credentials and region.

== Deploy Cluster ==

To deploy a 3 node cluster (1 leader, 2 followers)
./scripts/deploy.sh 3

== Cleanup cluster

./scripts/cleanup.sh 3

== Docker image ==

You can also modify Server.cpp and build your own Docker image, simply modify scripts/data/task-template.json and replace the image name with your own.