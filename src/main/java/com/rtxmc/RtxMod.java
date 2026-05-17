package com.rtxmc;

import com.rtxmc.render.VulkanRenderer;
import net.fabricmc.api.ClientModInitializer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public final class RtxMod implements ClientModInitializer {
    public static final String ID = "rtxmc";
    public static final Logger LOG = LoggerFactory.getLogger(ID);

    private static VulkanRenderer renderer;

    public static VulkanRenderer renderer() {
        return renderer;
    }

    @Override
    public void onInitializeClient() {
        LOG.info("rtxmc loading — native renderer will init after window creation");
        renderer = new VulkanRenderer();
    }
}
