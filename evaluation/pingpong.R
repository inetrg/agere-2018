times <- read.csv("evaluation/pingpong.csv", sep=",", as.is=c(numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric, numeric))
df <- times[-c(1), ]
df$avg <- rowMeans(df[,2:11])
df$sdev <- apply(df[,2:11], 1, sd)
df$upper_percentile <- apply(df[,2:11], 1, quantile, probs=c(0.95))
df$lower_percentile <- apply(df[,2:11], 1, quantile, probs=c(0.05))
pp_plot <- ggplot(df, aes(x=loss.percentage, y=avg/1000, color='color')) +
           geom_line(size=0.8) +
           # geom_point() +
           geom_errorbar(
             data=df,
             mapping=aes(
               x=loss.percentage,
               ymin=lower_percentile/1000,
               ymax=upper_percentile/1000
             ),
             # size=0.3,
             width=0.2) +
           scale_x_continuous(breaks=seq(0, 10, 1)) + # expand=c(0, 0), limits=c(0, 10)
           scale_y_continuous(limits=c(0, 62), breaks=seq(0, 60, 10)) + # expand=c(0, 0), limits=c(0, 10)
           theme_bw() +
           theme(
             legend.title=element_blank(),
             legend.position="none",
             text=element_text(size=9)
           ) +
           scale_color_brewer(type="qual", palette=7) +
           labs(x="Relative Loss on Link [%]", y="Runtime [s]")
ggsave("figs/pingpong.pdf", plot=pp_plot, width=3.4, height=2.3)
### tikz export
tikz(file="figs/pingpong.tikz", sanitize=TRUE, width=3.4, height=2.3)
pp_plot
dev.off()
