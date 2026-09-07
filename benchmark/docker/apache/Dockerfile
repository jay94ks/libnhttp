FROM httpd:2.4

# enable the event MPM (matches nginx's/nhttp's async model — the stock image
# ships prefork enabled and event/worker commented out) and keep-alive.
RUN sed -i \
      -e 's/^#LoadModule mpm_event_module/LoadModule mpm_event_module/' \
      -e 's/^LoadModule mpm_prefork_module/#LoadModule mpm_prefork_module/' \
      -e 's/^KeepAlive Off/KeepAlive On/' \
      -e 's/^MaxKeepAliveRequests .*/MaxKeepAliveRequests 100000/' \
      -e 's/^KeepAliveTimeout .*/KeepAliveTimeout 65/' \
      /usr/local/apache2/conf/httpd.conf \
    && printf '\nInclude conf/extra/httpd-mpm-bench.conf\n' >> /usr/local/apache2/conf/httpd.conf

# build context is benchmark/docker/ (see ../docker-compose.yml).
COPY apache/mpm_event.conf /usr/local/apache2/conf/extra/httpd-mpm-bench.conf
COPY content/index.html /usr/local/apache2/htdocs/index.html

EXPOSE 80
