FROM ubuntu:22.04

RUN apt-get update \
    && apt-get install unzip wget gcc make libpng-dev imagemagick -y \
    && apt-get install libsdl1.2-dev libsdl-image1.2-dev libhpdf-dev -y \
    && rm -rf /var/lib/apt/lists/* \
    && wget https://github.com/scott-wong/PrinterToPDF/archive/refs/heads/master.zip \
    && unzip PrinterToPDF-master.zip \
    && rm -rf PrinterToPDF-master.zip \
    && cd PrinterToPDF-master \
    && make && make install

WORKDIR /opt
CMD ["bash"]
