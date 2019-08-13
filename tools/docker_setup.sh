#!/usr/bin/env bash
#
#  Simple wrapper to run clang-format on a bunch of files
#
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.
#
# Copyright (C) 2019 Oath


set -e
set -o pipefail


readonly edgePrefix='edge:'
readonly ApacheRegistryHost='ci.trafficserver.apache.org'
Images=''
ImageSelection=''
ContainerName=''
CleanupCommands=''


function addToCleanup() {
	CleanupCommands="$1; $CleanupCommands"
	trap "echo 'Cleaning up...'; $CleanupCommands" EXIT
}

function runCleanupCommands() {
	eval "$CleanupCommands"
	trap : EXIT # Clear pending commands.
}

SCRIPTPATH="$( cd "$(dirname "$0")" ; pwd -P )"
local_file="$SCRIPTPATH/docker_setup_local.sh"
if [[ -r "$local_file" ]]
then
    source $local_file --source-only
else
    echo "Setup a $local_file to add your own images and options"
    #with
    #function populateCustomImages() {}
    #function overrideOptions() {}
fi

#varibles to override
OSEnv=''
YumPluginOvlCmd=''
RegistryBaseUrl=''
OSSpecific=''

function populateApacheImages() {
	for x in \
			'centos:6' \
			'centos:7' \
			'debian:8' \
			'debian:9' \
			'fedora:27' \
			'fedora:28' \
			'ubuntu:16.04' \
			'ubuntu:17.10'
	do
		Images="$Images ats:$x"
	done
}


function chooseImage() {
	echo 'Choose an image:'
	select ImageSelection in $Images; do break; done
	test -z "$ImageSelection" && exit 0
	IFS=':' read -r \
			ImagePlatform ImageName ImageVersion < \
			<(echo -n "$ImageSelection") || true
	ContainerName="${ImagePlatform}-${ImageName}_${ImageVersion}"
	LocalImageName="$(whoami)/${ContainerName}"
	LocalImageTag='final'
	echo "Using Image $ImageName, version $ImageVersion from $ImagePlatform."
}


function findExistingContainer() {
	readonly local Resource="${ImagePlatform}-${ImageName}_${ImageVersion}"
	readonly local Ancestor="$(whoami)/${Resource}:${LocalImageTag}"
	docker ps \
		--all \
		--filter ancestor="$Ancestor" \
		--format '{{.Names}}' \
	| \
	grep "^${ContainerName}$"
}


function createEntrypointFile() {
	cat > entrypoint.sh <<-EOF
		#!/usr/bin/env bash

		echo 'entrypoint.sh running'
		$OSEnv

		exec "\$@"
	EOF

	chmod -vv +x entrypoint.sh
}

function defaultOptions() {

    # init OSEnv
    if [[ 'centos' == "$ImageName" ]]
    then
        OSEnv="$OSEnv . /opt/rh/devtoolset-7/enable;"
        OSEnv="$OSEnv export LANG='en_US.UTF-8';"

    elif [[ 'debian' == "$ImageName" ]]
    then
        OSEnv= "$OSEnv \
            export CC=/usr/bin/gcc-7 \
            export CXX=/usr/bin/g++-7 \
            unset LANG \
            "
    fi

    # init YumPluginOvlCmd
    YumPluginOvlCmd='RUN sudo yum install -y yum-plugin-ovl gcc'
    if echo "$ImageSelection"|grep -e 'rhel7'
	then
		YumPluginOvlCmd="$YumPluginOvlCmd \
			--enablerepo=latest-rhel-7-server-rpms yum-plugin-ovl \
			|| true"
	fi

    # init RegistryBaseUrl
    RegistryBaseUrl="$ApacheRegistryHost"

    # init OSSpecific & append OSEnv
	if [[ 'ubuntu' == "$ImageName" || \
			'debian' == "$ImageName" ]]
	then
		Install='DEBIAN_FRONTEND=noninteractive apt-get install -y'
	    Update='DEBIAN_FRONTEND=noninteractive apt-get update'
        read -r -d '' OSSpecific <<-EOSNIPPET || true
			RUN $Update
			RUN $Install sudo || true
			# Development tools.
			RUN $Install vim screen ctags cscope
			# Dependencies for building ATS documentation
			RUN $Install python-pip graphviz
			RUN pip install --upgrade sphinx sphinx-rtd-theme sphinxcontrib-plantuml
		EOSNIPPET
	elif [[ 'centos' == "$ImageName" && 6 -ge "$ImageVersion" ]]
	then
		Install='yum install -y'
        read -r -d '' OSSpecific <<-EOSNIPPET || true
			RUN $Install sudo || true
			# This is needed to fix the overlay fs issue.
			# https://unix.stackexchange.com/questions/348941/rpmdb-checksum-is-invalid-trying-to-install-gcc-in-a-centos-7-2-docker-image#354658
			$YumPluginOvlCmd
			# Development tools.
			RUN $Install vim screen ctags cscope
			# Dependencies for building ATS documentation
			RUN $Install rh-python35-python-pip && \
					. /opt/rh/rh-python35/enable && \
					pip install --upgrade sphinx sphinx-rtd-theme sphinxcontrib-plantuml
			RUN $Install graphviz
		EOSNIPPET

        OSEnv="$OSEnv . /opt/rh/rh-python35/enable;"

	elif [[ 'fedora' == "$ImageName" || 'centos' == "$ImageName" ]]
	then
		Install='yum install -y'
        read -r -d '' OSSpecific <<-EOSNIPPET || true
			RUN $Install sudo || true
			# This is needed to fix the overlay fs issue.
			# https://unix.stackexchange.com/questions/348941/rpmdb-checksum-is-invalid-trying-to-install-gcc-in-a-centos-7-2-docker-image#354658
			$YumPluginOvlCmd
			# Development tools.
			RUN $Install vim screen ctags cscope
			# Dependencies for building ATS documentation
			RUN $Install rh-python35-python-pip && \
					. /opt/rh/rh-python35/enable && \
                    pip install --upgrade sphinx sphinx-rtd-theme sphinxcontrib-plantuml
			RUN $Install graphviz
		EOSNIPPET

        OSEnv="$OSEnv . /opt/rh/rh-python35/enable;"
	fi

}


function createDockerfile() {
	createEntrypointFile

	# The CONTAINER environment variable has a special format for edge.
	readonly CONTAINER=$ImageSelection

	cat > Dockerfile <<-EOF
		FROM "${RegistryBaseUrl}/${ImagePlatform}/${ImageName}:${ImageVersion}"
		LABEL maintainer "$(whoami)"


		ENV CONTAINER $CONTAINER
		COPY entrypoint.sh /entrypoint.sh

		$OSSpecific
		RUN sudo whoami
		RUN sudo useradd --home-dir '$HOME' --no-create-home $(whoami)
		RUN echo '$(whoami) ALL = (ALL) NOPASSWD: ALL' | sudo tee -a /etc/sudoers
		USER $(whoami)
		WORKDIR $HOME

		ENTRYPOINT ["/entrypoint.sh"]
		CMD ["/bin/bash"]
	EOF
}


function buildDockerImage() {
	readonly local TempDir="$(mktemp -d /tmp/$(basename -- "$0").XXXXXX)" \
		|| (echo 'Failed to create a temporary directory.'>&2 && exit 1)
	addToCleanup "rm -rvf '$TempDir'"
	pushd "$TempDir" > /dev/null
	addToCleanup 'popd > /dev/null'

    defaultOptions
    declare -F overrideOptions && overrideOptions
	createDockerfile
	docker build --squash -f Dockerfile -t "${LocalImageName}:${LocalImageTag}" . \
		|| ( echo 'Could not build docker image' >&2 &&  exit 1)
}


function findExistingImage() {
	docker image ls --format '{{.Repository}}:{{.Tag}}' \
	| \
	grep "^${LocalImageName}:${LocalImageTag}"
}


function createImageAndRunContainer() {
	readonly local ExistingImage="$(findExistingImage)"
	if [[ -n "$ExistingImage" ]]
	then
		echo "Using existing image '$ExistingImage'..."
	else
		buildDockerImage
	fi

	runCleanupCommands

	docker run \
		--interactive \
		--tty \
		--init \
		--volume "$HOME":"$HOME":delegated \
		--network=host \
		--name "$ContainerName" \
		--privileged \
		"$LocalImageName:$LocalImageTag" \
	|| ( echo 'Failed to create container' >&2 && exit 1 )
}


function main() {

    declare -F populateCustomImages && populateCustomImages
	populateApacheImages
	chooseImage

	readonly local ExistingContainer="$(findExistingContainer)"
	if [[ -n "$ExistingContainer" ]]
	then
		echo "Using existing container '$ExistingContainer'..."
		if [[ 'true' == \
			"$(docker inspect -f '{{.State.Running}}' "$ExistingContainer")" ]]
		then
			docker exec --interactive --tty "$ExistingContainer" \
					/entrypoint.sh /bin/bash
		else
			docker start --interactive "$ExistingContainer"
		fi
	else
		createImageAndRunContainer
	fi
}


if [[ "$(basename -- "$0")" == 'docker_setup.sh' || \
		"$(basename -- "$0")" == 'docker_setup' ]]
then
	if [[ $# -gt 0 ]]
	then
		cat <<-EOHELP
			Usage: $0

			Enter a shell in an ATS development environment.

			Select the image you wish to use from among the Edge
			(edge) and public ATS CI images (ats). The selected image is
			overlayed with certain packages beneficial to development (e.g., vim).
			The result is used to start a Docker container.

			A bash shell will execute in your mounted home directory running as the
			same user as on the host. It sources an environment script to set up paths
			to the correct toolchain.

			For performance reasons, mount synchronization is 'delegated' and the
			network mode is 'host'. Refer to Docker documentation.

			No effort is made to prune docker images and containers. However, if a
			local Docker container exists for a given Edge container, it will
			be reused rather than a new one created.
		EOHELP
		exit 0
	fi

	main
fi
