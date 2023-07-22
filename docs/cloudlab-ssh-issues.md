# CloudLab SSH

This document contains some issues users may run into when attempting to run
 a distbench experiment on CloudLab using the `deploy_to_cloudlab.sh` script.

## Username

One of the common issues is that the username on your device must match your 
 cloudlab usrname in order to SSH into a node in your experiment. Currently,
 our solution to this issue is to set an environment variable named
 `CLOUDLAB_USER` to your cloudlab username before running the script.
 ``` bash
export CLOUDLAB_USER=username
 ```

## Issues with SSH keys and Homa install

Another common error that occurs in the context of running the install script from the homa module repo is that the local key agent does not attempt
 to use the proper SSH key. To diagnose this error, we recommend rerunning the
 command that caused the SSH error and adding a `-v` flag The 
 verbose mode will print all of the SSH keys it attemps to use, and if none
 of the keys match the key you uploaded to CloudLab then this is your issue.
 We solved this issue by running the commands:
 ``` bash
mv key_i_want.pub id_rsa.pub
mv key_i_want id_rsa
 ```
 The first key should match the key used on CloudLab and the second SSH key
 should be a key that the agent attempts to use.

In case you are having problems to ssh from one node to another in a cloudlab cluster, you can create a new SSH key in your local machine with key-gen, add the public key to your cloudlab ssh keys and copy the private key to node0 with scp. 

You might have to rename the key inside of node0 to the default name id_rsa which is one of the names the SSH agent will search for when trying to do SSH.

