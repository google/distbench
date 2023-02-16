################################################################################
# Copyright 2023 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################
#
# common_status.sh:
# Common functions for enhancing the visibility of important status messages.
#
# A script can set the variable ECHODELAY before sourcing this file to adjust
# the speed of status message display.
#
# This code is written in such a way so as to avoid making debugging with set -x
# needlessly verbose. As a result, the speed specified by ECHODELAY is set
# once-and-for-all and cannot be adjusted later.

function build_command() {
  echo_in_color="(tput bold; tput setaf \${0}; echo -e \"\${@}\"; tput sgr0)"
  local DELAY="${ECHODELAY:-0.003}"
  if [[ "${DELAY}" != 0 ]]
  then
    echo_in_color+=" | while read -N1 c; do sleep $DELAY; echo -n \"\$c\"; done"
  fi
  readonly echo_in_color
}

build_command
unset build_command

function echo_black() { bash -c "$echo_in_color" 0 "${@}"; }
function echo_red() { bash -c "$echo_in_color" 1 "${@}"; }
function echo_green() { bash -c "$echo_in_color" 2 "${@}"; }
function echo_yellow() { bash -c "$echo_in_color" 3 "${@}"; }
function echo_blue() { bash -c "$echo_in_color" 4 "${@}"; }
function echo_magenta() { bash -c "$echo_in_color" 5 "${@}"; }
function echo_cyan() { bash -c "$echo_in_color" 6 "${@}"; }
function echo_white() { bash -c "$echo_in_color" 7 "${@}"; }
function echo_error() { local c="$1"; shift; echo_$c "${@}" 1>&2 ; }
