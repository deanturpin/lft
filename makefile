all: readme.md

github = https://raw.githubusercontent.com/deanturpin

readme.md: all_orders.csv
	@echo '<pre>' > $@
	cat all_orders.csv >> $@
	@echo '<pre>' >> $@

orders.csv:
	curl $(github)/handt/gh-pages/orders.csv > $@

clean:
	rm -f orders.csv

all_orders.csv: orders.csv
	rm -f $@
	curl $(github)/lft/gh-pages/all_orders.csv> $@
	@echo $(shell TZ=BST-2 date) >> $@
	cat $< >> $@
	@echo >> $@
