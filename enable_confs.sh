#!/bin/bash

./scripts/config -e CONFIG_RCU_EXPERT
./scripts/config -e CONFIG_RCU_NOCB_CPU
./scripts/config -e CONFIG_RCU_NOCB_CPU_DEFAULT_ALL
./scripts/config -e CONFIG_RCU_LAZY
./scripts/config -e CONFIG_RCU_CB_DEBUG
