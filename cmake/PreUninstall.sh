if [ $1 == 0 ]; then
	DST=${RPM_INSTALL_PREFIX}
	
	touch ${DST}/plug/conflist.inf

	sed -i '/shaping_master.inf/d' ${DST}/plug/conflist.inf
fi

