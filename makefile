readme.md: orders.csv
	@echo '<pre>' > $@
	cat orders.csv >> $@
	@echo '<pre>' >> $@

orders.csv:
	curl https://raw.githubusercontent.com/deanturpin/handt/gh-pages/orders.csv > $@

clean:
	rm -f orders.csv
