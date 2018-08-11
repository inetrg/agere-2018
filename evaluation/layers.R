df <- read.csv("layers.csv", sep = ",", skip = 8)
# Clean empty columns.
emptycols <- sapply(df, function (k) all(is.na(k)))
df <- df[!emptycols]
# Add a tag to differentiatae between udp and tcp tests.
df$proto <- data.frame(ifelse(grepl("udp", df$name), "udp", "tcp"))
# Separate test input size from name.
df$name <- data.frame(do.call('rbind', strsplit(as.character(df$name), '/', fixed=TRUE)))

# Get UDP related data.
udp <- split(df,df$proto)[['udp']]
# Convert Size value to numerics
udp$name$X1 <- as.character(udp$name$X1)
udp$name$X2 <- as.numeric(as.character(udp$name$X2))
# Prepare splitting data into send and receive by adding a tag.
udp$operation <- data.frame(ifelse(grepl("send", udp$name$X1), "send", "receive"))
# Get UDP send tests.
udp_send <- split(udp,udp$operation)[['send']]
udp_send$name$X1 <- gsub("BM_send<raw_data_message, udp_protocol<raw>>",                     "raw",             udp_send$name$X1)
udp_send$name$X1 <- gsub("BM_send<raw_data_message, udp_protocol<ordering<raw>>>",           "ordering",        udp_send$name$X1)
udp_send$name$X1 <- gsub("BM_send<new_basp_message, udp_protocol<datagram_basp>>",           "basp",            udp_send$name$X1)
udp_send$name$X1 <- gsub("BM_send<new_basp_message, udp_protocol<ordering<datagram_basp>>>", "ordering + basp", udp_send$name$X1)
udp_send_plot <- ggplot(udp_send, aes(x=name$X2, y=real_time / 1000, color=name$X1)) +
                 geom_line() +
                 theme_bw() +
                 theme(
                   legend.title = element_blank(),
                   legend.position = "top",
                   text=element_text(size=9)
                 ) +
                 labs(x="Packet Size [bytes]", y="Time [ms]")
ggsave("udp_send.pdf", plot=udp_send_plot, width=3.4, height=2.3)
# Get UDP receive tests.
udp_receive <- split(udp,udp$operation)[['receive']]
udp_receive$name$X1 <- gsub("BM_receive<raw_data_message, udp_protocol<raw>>",                     "raw",             udp_receive$name$X1)
udp_receive$name$X1 <- gsub("BM_receive<raw_data_message, udp_protocol<ordering<raw>>>",           "ordering",        udp_receive$name$X1)
udp_receive$name$X1 <- gsub("BM_receive<new_basp_message, udp_protocol<datagram_basp>>",           "basp",            udp_receive$name$X1)
udp_receive$name$X1 <- gsub("BM_receive<new_basp_message, udp_protocol<ordering<datagram_basp>>>", "ordering + basp", udp_receive$name$X1)
# Had to rename some benchmarks.
udp_receive$name$X1 <- gsub("BM_receive_udp_raw",           "raw",             udp_receive$name$X1)
udp_receive$name$X1 <- gsub("BM_receive_udp_ordering_raw",  "ordering",        udp_receive$name$X1)
udp_receive$name$X1 <- gsub("BM_receive_udp_basp",          "basp",            udp_receive$name$X1)
udp_receive$name$X1 <- gsub("BM_receive_udp_ordering_basp", "ordering + basp", udp_receive$name$X1)
udp_receive_plot <- ggplot(udp_receive, aes(x=name$X2, y=real_time / 1000, color=name$X1)) +
                           geom_line() +
                           theme_bw() +
                           theme(
                             legend.title = element_blank(),
                             legend.position = "top",
                             text=element_text(size=9)
                           ) +
                           labs(x="Packet Size [bytes]", y="Time [ms]")
ggsave("udp_receive.pdf", plot=udp_receive_plot, width=3.4, height=2.3)




# Get tcp related data.
tcp <- split(df,df$proto)[['tcp']]
tcp$name$X1 <- as.character(tcp$name$X1)
tcp$name$X2 <- as.numeric(as.character(tcp$name$X2))
# Rename things.
tcp$name$X1 <- gsub("BM_send<raw_data_message, tcp_protocol<raw>>",         "raw",  tcp$name$X1)
tcp$name$X1 <- gsub("BM_send<new_basp_message, tcp_protocol<stream_basp>>", "basp", tcp$name$X1)
tcp_plot <- ggplot(tcp, aes(x=name$X2, y=real_time / 1000, color=name$X1)) +
                   geom_line() +
                   theme_bw() +
                   theme(
                     legend.title = element_blank(),
                     legend.position = "top",
                     text=element_text(size=9)
                   ) +
                   labs(x="Packet Size [bytes]", y="Time [ms]")
ggsave("tcp_send.pdf", plot=tcp_plot, width=3.4, height=2.3)
