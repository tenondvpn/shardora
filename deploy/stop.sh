#!/bin/bash

ps -ef | grep zjchain | awk -F' ' '{print $2}' | xargs kill -9
