# edgerq

The edgerq tool kit makes it easy to create reverse tunnels and simple overlay networks running through UDP without any complex set-up and access different types of TCP-based services anywhere - be it in a locally hosted infrastructure without a public IP and/or to further route requests to systems that would otherwise not be able to serve these requests.

![Basic Schematics](schemas/schema_basic.jpg)

# edgerq_sc

"SC" stands for Service Consumer. In the EDGERQ tool kit, the SC is meant to act as a server for edgerq_sp from a networking point of view, but in fact, is used to listen on specified ports for HTTP requests and forward them to edgerq_sp through a connection established by edgerq_sp which in turn routes the requests to specified web servers and get back with responses to edgerq_sc.

One use case is that you would install edgerq_sc on a remote server and edgerq_sp in your local infrastructure. You would then configure edgerq_sp to connect to edgerq_sc using UDP. edgerq_sc would be set up to listen for HTTP requests forwarding them back to edgerq_sp to web servers running in your local infrastructure.

It is advised that you do not expose the ports on which edgerq_sc is listening for HTTP requests on a public IP, but instead on localhost on the public server and you forward requests to it from a web interface that needs these other services.

![SC Service Consumer Schematics](schemas/schema_edgerqSc_setup_stunnel.jpg)

consumer.xml sample:

