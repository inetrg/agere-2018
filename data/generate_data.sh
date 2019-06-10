#!/bin/bash

function generate() {
	dd if=/dev/urandom iflag=fullblock of=$1-file bs=$1 count=1
}

generate 1M
generate 10M
generate 100M
generate 1G
