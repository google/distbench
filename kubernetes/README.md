# Running Distbench in Kubernetes

This document describes the step to run Distbench in a Kubernetes cluster on
Google Cloud Platform.

## 1. Service account

1. Create a service account (see
   https://cloud.google.com/iam/docs/creating-managing-service-accounts)
2. Create and download the keys for the service account (see
   https://cloud.google.com/iam/docs/creating-managing-service-account-keys)
3. Activate the service account on your system, using the matching keyfile
   ```bash
   gcloud auth activate-service-account name@project.iam.gserviceaccount.com --key-file keyfile_sa 
   ```
4. gcloud auth configure-docker

## 2. Build and Push the Distbench Docker image

```bash
export CLOUD_ENGINE_PROJECT=project_name
./build_push_docker_image.sh
```

### 3. Create a cluster and apply the Kubernetes configuration


```bash
./create_kubernetes.sh
./kube_deploy.sh

```
