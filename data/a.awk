#!/usr/bin/awk -f

{
	f += $1
	fs[n] = $1
	t += $2
	ts[n] = $2
	++n
}

END {
	f_avg = f / NR
	t_avg = t / NR

	for (i in fs) {
		f_dev += (fs[i] - f_avg) * (fs[i] - f_avg)
	}
	for (i in ts) {
		t_dev += (ts[i] - t_avg) * (ts[i] - t_avg)
	}
	f_dev = sqrt(f_dev / (n - 1))
	t_dev = sqrt(t_dev / (n - 1))

	print FILENAME
	print "FPS: ", f_avg, f_dev
	print "time: ", t_avg, t_dev
	print "--------"
}
