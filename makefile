all: readme.md

github = https://raw.githubusercontent.com/deanturpin

# Append recent orders to readme
readme.md: orders.csv
	@echo '$(shell TZ=BST-1 date)' >> $@
	@echo '<pre>' >> $@
	cat $< >> $@
	@echo '</pre>' >> $@

orders.csv:
	curl $(github)/handt/gh-pages/orders.csv > $@

clean:
	rm -f orders.csv
