<?xml version="1.0"?>
<!-- Customization layer -->

<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version="1.0">

 <!-- Use 'chunk.xsl' in line below to chunk files. -->
 <xsl:import href="link-xhtml/xhtml/docbook.xsl"/>

 <xsl:param name="chapter.autolabel" select="1"/>
 <xsl:param name="section.autolabel" select="1"/>
 <xsl:param name="section.label.includes.component.label" select="1" />
 
 <xsl:param name="chunk.first.sections" select="1"/>
 
 <xsl:param name="chunk.section.depth" select="2"/>
 
 <!-- Should graphics be used for admonitions (notes, warnings)? 0 or 1 -->
 <xsl:param name="admon.graphics" select="1"/>
 
 <!-- If using admon graphics (1 above), what is path to graphics? 
      Should be the path relative to your document and MUST end with
      a trailing slash.  Also, this parameter needs to be on a 
      single line. -->
 <xsl:param name="admon.graphics.path">images/</xsl:param>
 <xsl:param name="admon.graphics.extension" select="'.png'"/>
 
 <!-- When chunking, use id attribute as filename? 0 or 1 -->
 <xsl:param name="use.id.as.filename" select="1"/>
 
 <xsl:param name="shade.verbatim" select="0"/>
 
 <xsl:attribute-set name="shade.verbatim.style">
     <xsl:attribute name="border">0</xsl:attribute>
     <xsl:attribute name="bgcolor">#E0E0E0</xsl:attribute>
 </xsl:attribute-set>

 <xsl:param name="html.stylesheet" select="'style.css'"/>
 <!-- <xsl:param name="toc.list.type">ul</xsl:param>-->
 <xsl:param name="toc.section.depth">3</xsl:param>

 <xsl:param name="funcsynopsis.style">ansi</xsl:param>

</xsl:stylesheet>
