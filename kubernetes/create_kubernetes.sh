#!/bin/bash

gcloud container clusters create $USER-distbench --num-nodes 2 --zone us-central1-a
