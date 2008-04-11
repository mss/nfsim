
.PHONY: docs
docs: html

html:	simulator.html

pdf:	simulator.pdf

%.html:	%.xml.gen xhtml.xsl
	xsltproc xhtml.xsl $< | xmllint --format - > $@

%.pdf: %.xml.gen fo.xsl
	fop -xml $< -xsl fo.xsl -pdf $@

.INTERMEDIATE: simulator.xml.gen
simulator.xml.gen: simulator.xml links
	./autogen-docs $< $@

%.xml.gen: %.xml links
	cp $< $@

.PHONY:	clean
clean:
	rm -f *.html *.pdf

.PHONY: distclean
distclean:
	rm -f link-xhtml link-dtd

.PHONY:	check
check: 
	xmllint --noout --valid simulator.xml

# Create links to docbook conmponents
links: 
	./create-links
