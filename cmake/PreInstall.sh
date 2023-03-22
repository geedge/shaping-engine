DST=${RPM_INSTALL_PREFIX}

mkdir -p ${DST}/plug/platform/
touch ${DST}/plug/conflist.inf


if [[ -z `grep -rn 'shaping_master.inf' ${DST}/plug/conflist.inf` ]];then
	sed -i '/\[platform\]/a\./plug/platform/shaping_master/shaping_master.inf' ${DST}/plug/conflist.inf
fi

