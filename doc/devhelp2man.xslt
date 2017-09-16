<!--
Copyright © 2017 Collabora Ltd

This file is part of libcapsule.

libcapsule is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as
published by the Free Software Foundation; either version 2.1 of the
License, or (at your option) any later version.

libcapsule is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with libcapsule.  If not, see <http://www.gnu.org/licenses/>.
-->

<xsl:stylesheet version = '1.0'
                xmlns:xi="http://www.w3.org/2003/XInclude"
                xmlns:xsl='http://www.w3.org/1999/XSL/Transform'
                xmlns:saxon="http://icl.com/saxon"
                xmlns:xslt="http://xml.apache.org/xslt"
                xmlns:string="http://exslt.org/strings"
                xmlns:ext="http://exslt.org/common"
                extension-element-prefixes="saxon"
                exclude-result-prefixes="xi">
  <xsl:strip-space elements="*" />
  <xsl:output omit-xml-declaration="yes"
              indent="yes"
              encoding="utf-8"
              xslt:indent-amount="2"/>

  <xsl:template match="node()|@*" mode="description">
    <xsl:apply-templates/>
  </xsl:template>

  <xsl:template name="paramdesc" match="refsect2[@role='function']"
                mode="paramdesc">
    <xsl:param name="matched" select="indexterm/primary"/>
    <xsl:if test="$matched = $target">
      <xsl:element name="title">Parameters:</xsl:element>
      <xsl:element name="variablelist">
        <xsl:for-each select="refsect3[@role='parameters']//row">
          <xsl:element name="varlistentry">
            <xsl:element name="term">
              <xsl:element name="code">
                <xsl:value-of select="concat(entry[@role='parameter_name'], ': ')"/>
              </xsl:element>
            </xsl:element>
            <xsl:element name="listitem">
              <xsl:element name="para">
                <xsl:value-of select="entry[@role='parameter_description']"/>
              </xsl:element>
            </xsl:element>
          </xsl:element>
        </xsl:for-each>
      </xsl:element>
    </xsl:if>
  </xsl:template>

  <xsl:template name="retdesc" match="refsect2[@role='function']"
                mode="retdesc">
    <xsl:param name="matched" select="indexterm/primary"/>
    <xsl:if test="$matched = $target">
      <xsl:for-each select="refsect3[@role='returns']/*">
        <xsl:copy-of select="."/>
      </xsl:for-each>
    </xsl:if>
  </xsl:template>
  <!-- //////////////////////////////////////////////////////// -->

  <xsl:template match="node()|@*" name="ignore">
    <xsl:apply-templates/>
  </xsl:template>

  <xsl:template match="entry[@role='function_name']/link" name="refnamediv">
    <xsl:param name="matched" select="."/>
    <xsl:if test="$matched = $target">
      <xsl:element name="refname">
        <xsl:value-of select="."/>
      </xsl:element>
      <xsl:apply-templates/>
    </xsl:if>
  </xsl:template>

  <xsl:template name="paramdef" match="parameter" mode="funcsig">
    <xsl:param name="tok"   select="string:tokenize(., ' ')"/>
    <xsl:param name="last"  select="ext:node-set($tok)[last()]"/>
    <xsl:param name="name"  select="string:tokenize($last, '*')"/>
    <xsl:param name="stars" select="substring-before($last, $name)"/>
    <xsl:element name="paramdef">
      <xsl:for-each select="ext:node-set($tok)[position() != last()]">
        <xsl:value-of select="concat(., ' ')"/>
      </xsl:for-each>
      <xsl:value-of select="$stars"/>
      <xsl:element name="parameter">
        <xsl:value-of select="$name"/>
      </xsl:element>
    </xsl:element>
  </xsl:template>

  <xsl:template name="funcproto" match="refsect2[@role='function']">
    <xsl:param name="matched" select="indexterm/primary"/>
    <xsl:if test="$matched = $target">
      <xsl:element name="funcprototype">
        <xsl:element name="funcdef">
          <xsl:value-of
              select="concat(substring-before(programlisting,'&#xa;'), ' ')"/>
          <xsl:element name="function">
            <xsl:value-of select="indexterm/primary"/>
          </xsl:element>
        </xsl:element>
        <xsl:apply-templates select="programlisting" mode="funcsig"/>
      </xsl:element>
    </xsl:if>
  </xsl:template>

  <xsl:template name="voiddesc" match="refsect2[@role='function']"
                mode="voiddesc">
    <xsl:param name="matched" select="indexterm/primary"/>
    <xsl:if test="$matched = $target">
      <xsl:for-each select="para">
        <xsl:copy-of select="."/>
      </xsl:for-each>
    </xsl:if>
  </xsl:template>

  <xsl:template match="/refentry" name="top">
    <xsl:element name="refentry">
      <xsl:attribute name="id"><xsl:value-of select="@id"/></xsl:attribute>
      <xsl:element name="refentryinfo">
        <xsl:element name="productname">libcapsule</xsl:element>
        <xsl:element name="authorgroup">
          <xsl:element name="contrib">Author</xsl:element>
          <xsl:element name="firstname">Vivek</xsl:element>
          <xsl:element name="surname">Das Mohapatra</xsl:element>
        </xsl:element>
      </xsl:element>
      <xsl:element name="refmeta">
        <xsl:element name="refentrytitle">
          <!-- xsl:value-of select="/refentry/refmeta/refentrytitle"/ -->
          <xsl:value-of select="$target"/>
        </xsl:element>
        <xsl:element name="manvolnum">
          <xsl:value-of select="/refentry/refmeta/manvolnum"/>
        </xsl:element>
        <xsl:element name="refmiscinfo">
          <xsl:attribute name="class">manual</xsl:attribute>
          <xsl:value-of select="normalize-space(refmeta/refmiscinfo)"/>
        </xsl:element>
      </xsl:element>
      <xsl:element name="refnamediv">
        <xsl:apply-templates select="refsect1[@role='functions_proto']"/>
      </xsl:element>
      <xsl:element name="refsynopsisdiv">
        <xsl:element name="funcsynopsis">
          <xsl:element name="funcsynopsisinfo">
            <xsl:value-of select="$fsinfo"/>
          </xsl:element>
        </xsl:element>
        <xsl:apply-templates select="refsect1[@role='details']"/>
      </xsl:element>
      <xsl:element name="refsect1">
        <xsl:element name="title">Description:</xsl:element>
        <xsl:apply-templates
            select="refsect1[@role='details' and contains(@id,'functions_details')]/refsect2"
            mode="voiddesc"/>
        <xsl:element name="refsect2">
          <xsl:apply-templates
              select="refsect1[@role='details' and contains(@id,'functions_details')]/refsect2"
                               mode="paramdesc"/>
        </xsl:element>
        <xsl:element name="refsect2">
          <xsl:apply-templates
              select="refsect1[@role='details' and contains(@id,'functions_details')]/refsect2"
              mode="retdesc"/>
        </xsl:element>
      </xsl:element>

      <xsl:element name="refsect1">
        <xsl:element name="title">See Also:</xsl:element>
          <xsl:element name="para">
            <xsl:for-each select="refsect1[@role='details']/refsect2[@role='function']/indexterm/primary">
              <xsl:element name="link">
                <xsl:element name="function">
                  <xsl:value-of select="concat(.,'(3)')"/>
                </xsl:element>
              </xsl:element>
              <xsl:choose>
                <xsl:when test="position() != last()">
                  <xsl:text>, </xsl:text>
                </xsl:when>
              </xsl:choose>
            </xsl:for-each>
          </xsl:element>
      </xsl:element>

    </xsl:element>
  </xsl:template>

</xsl:stylesheet>
