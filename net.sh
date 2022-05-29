while:
do
	for f in ./pdf/*.pdf
	do
		ln $f
		rm $f
	done
	nc -l -p 8080 -q 20 -N | ./printerToPDF -f ./font2/Epson-Standard.C16 -9 -q -o ./
done


