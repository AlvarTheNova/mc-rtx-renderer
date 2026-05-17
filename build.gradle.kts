plugins {
    id("fabric-loom") version "1.9-SNAPSHOT"
    `java-library`
}

val minecraftVersion: String by project
val yarnMappings: String by project
val loaderVersion: String by project
val fabricVersion: String by project
val modVersion: String by project
val mavenGroup: String by project
val archivesBaseName: String by project
val lwjglVersion: String by project

version = modVersion
group = mavenGroup
base.archivesName.set(archivesBaseName)

java {
    toolchain.languageVersion.set(JavaLanguageVersion.of(21))
    withSourcesJar()
}

repositories {
    maven("https://maven.fabricmc.net/")
    mavenCentral()
}

dependencies {
    minecraft("com.mojang:minecraft:$minecraftVersion")
    mappings("net.fabricmc:yarn:$yarnMappings:v2")
    modImplementation("net.fabricmc:fabric-loader:$loaderVersion")
    modImplementation("net.fabricmc.fabric-api:fabric-api:$fabricVersion")

    // LWJGL Vulkan bindings — MC already pulls LWJGL core, but vulkan module
    // is not a default. We use it only for surface creation in Java; the hot
    // path lives in the native lib.
    implementation("org.lwjgl:lwjgl-vulkan:$lwjglVersion")
}

loom {
    accessWidenerPath.set(file("src/main/resources/rtxmc.accesswidener"))
    runs {
        named("client") {
            // Force NVIDIA discrete GPU on laptops with hybrid graphics.
            vmArgs("-Dorg.lwjgl.util.Debug=true")
            // Streamline interposer needs to find sl.* plugin DLLs.
            vmArgs("-Djava.library.path=${projectDir}/native/build/Release;${projectDir}/streamline/bin/x64")
        }
    }
}

tasks.withType<JavaCompile>().configureEach {
    options.release.set(21)
    options.encoding = "UTF-8"
}

tasks.processResources {
    inputs.property("version", project.version)
    filesMatching("fabric.mod.json") {
        expand("version" to project.version, "mc_version" to minecraftVersion)
    }
}

// Convenience: build the native lib before assembling the mod jar.
tasks.register<Exec>("buildNative") {
    workingDir = file("native")
    commandLine("cmake", "--build", "build", "--config", "Release")
}
tasks.named("assemble") { dependsOn("buildNative") }
