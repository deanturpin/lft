readme.md: all_orders.csv
	@echo '<pre>' > $@
	cat all_orders.csv >> $@
	@echo '<pre>' >> $@

orders.csv:
	curl https://raw.githubusercontent.com/deanturpin/handt/gh-pages/orders.csv > $@

clean:
	rm -f orders.csv

all_orders.csv: orders.csv
	@echo $(shell TZ=BST-2 date) >> $@
	cat $< >> $@
	@echo >> $@
