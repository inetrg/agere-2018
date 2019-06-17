library(tikzDevice)
library(ggplot2)
require(scales)
require(RColorBrewer)
require(gridExtra)

pptcp <- read.csv("csv/tcp-data.csv", sep=",", as.is=c(numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric))

pptcp$avg <- rowMeans(pptcp[,2:11])
pptcp$sdev <- apply(pptcp[,2:11], 1, sd)
pptcp$proto <- 'TCP-newb'

ppquic <- read.csv("csv/quic-data.csv", sep=",", as.is=c(numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric))

ppquic$avg <- rowMeans(ppquic[,2:11])
ppquic$sdev <- apply(ppquic[,2:11], 1, sd)
ppquic$proto <- 'QUIC'

pptradtcp <- read.csv("csv/traditional-tcp-data.csv", sep=",", as.is=c(numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric))

pptradtcp$avg <- rowMeans(pptcp[,2:11])
pptradtcp$sdev <- apply(pptcp[,2:11], 1, sd)
pptradtcp$proto <- 'TCP-traditional'

ppdf <- rbind(pptcp, ppquic)
ppdf <- rbind(ppdf, pptradtcp)
ppdf$upper <- ppdf$avg + ppdf$sdev
ppdf$lower <- ppdf$avg - ppdf$sdev

pp_plot <- ggplot(ppdf, aes(x=size, y=avg/1000, color=proto)) +
           geom_line() + # size=0.8) +
           geom_point(aes(shape=proto), size = 2, stroke=0.8) +
           geom_errorbar(
             mapping=aes(
               ymin=lower/1000,
               ymax=upper/1000
             ),
             width=0.2
           ) +
           scale_shape_manual(values=c(1, 4, 3)) +
           #scale_x_continuous(breaks=seq(0, 1073741824, )) + # expand=c(0, 0), limits=c(0, 10)
           scale_x_log10(breaks = trans_breaks("log10", function(x) 10^x),
                         labels = trans_format("log10", math_format(10^.x))) +
           #scale_y_continuous(limits=c(0, 40), breaks=seq(0, 40, 10)) +
           scale_y_log10() +
           theme_bw() +
           theme(
             legend.title=element_blank(),
             legend.key=element_rect(fill='white'), 
             legend.background=element_rect(fill="white", colour="black", size=0.25),
             legend.direction="vertical",
             legend.justification=c(0, 1),
             legend.position=c(0, 1),
             legend.box.margin=margin(c(3, 3, 3, 3)),
             legend.key.size=unit(0.8, 'lines'),
             text=element_text(size=9),
             strip.background=element_blank(),
             strip.text.x=element_blank()
           ) +
           # scale_color_grey() +
           scale_color_brewer(type="qual", palette=6) +
           labs(x="Bytes sent [Byte]", y="Runtime [s]")

tikz(file="figs/data-transfer.tikz", sanitize=TRUE, width=3.4, height=2.3)
pp_plot
dev.off()
ggsave("figs/data-transfer.pdf", plot=pp_plot, width=3.4, height=2.3)



