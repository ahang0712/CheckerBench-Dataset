# Patch-overlapping Java context after the fix

## `src/main/java/co/uk/mommyheather/advancedbackups/cli/AdvancedBackupsCLI.java` (changed lines (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 256, 257, 258, 259, 260, 261, 262, 263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 300, 301, 302, 303, 304, 305, 306, 307, 308, 309, 310, 311, 312, 313, 314, 315, 316, 317, 318, 319, 320, 321, 322, 323, 324, 325, 326, 327, 328, 329, 330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 345, 346, 347, 348, 349, 350, 351, 352, 353, 354, 355, 356, 357, 358, 359, 360, 361, 362, 363, 364, 365, 366, 367, 368, 369, 370, 371, 372, 373, 374, 375, 376, 377, 378, 379, 380, 381, 382, 383, 384, 385, 386, 387, 388, 389, 390, 391, 392, 393, 394, 395, 396, 397, 398, 399, 400, 401, 402, 403, 404, 405, 406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416, 417, 418, 419, 420, 421, 422, 423, 424, 425, 426, 427, 428, 429, 430, 431, 432, 433, 434, 435, 436, 437, 438, 439, 440, 441, 442, 443, 444, 445, 446, 447, 448, 449, 450, 451, 452, 453, 454, 455, 456, 457, 458, 459, 460, 461, 462, 463, 464, 465, 466, 467, 468, 469, 470, 471, 472, 473, 474, 475, 476, 477, 478, 479, 480, 481, 482, 483, 484, 485, 486, 487, 488, 489, 490, 491, 492, 493, 494, 495, 496, 497, 498, 499, 500, 501, 502, 503, 504, 505, 506, 507, 508, 509, 510, 511, 512, 513, 514, 515, 516, 517, 518, 519, 520, 521, 522, 523, 524, 525, 526, 527, 528, 529, 530, 531, 532, 533, 534, 535, 536, 537, 538, 539, 540, 541, 542, 543, 544, 545, 546, 547, 548, 549, 550, 551, 552, 553, 554, 555, 556, 557, 558, 559, 560, 561, 562, 563, 564, 565, 566, 567, 568, 569, 570, 571, 572, 573, 574, 575, 576, 577, 578, 579, 580, 581, 582, 583, 584, 585, 586, 587, 588, 589, 590, 591, 592, 593, 594, 595, 596, 597, 598, 599, 600, 601, 602, 603, 604, 605, 606, 607, 608, 609, 610, 611, 612, 613, 614, 615, 616, 617, 618, 619, 620, 621, 622, 623, 624, 625, 626, 627, 628, 629, 630, 631, 632, 633, 634, 635, 636, 637, 638, 639, 640, 641, 642, 643, 644, 645, 646, 647, 648, 649, 650, 651, 652, 653, 654, 655, 656, 657, 658, 659, 660, 661, 662, 663, 664, 665, 666, 667, 668, 669, 670, 671, 672, 673, 674, 675, 676, 677, 678, 679, 680, 681, 682, 683, 684, 685, 686, 687, 688, 689, 690, 691, 692, 693, 694, 695, 696, 697, 698, 699, 700, 701, 702, 703, 704, 705, 706, 707, 708, 709, 710, 711, 712, 713, 714, 715, 716, 717, 718, 719, 720, 721, 722, 723, 724, 725, 726, 727, 728, 729, 730, 731, 732, 733, 734, 735, 736, 737, 738, 739, 740, 741, 742, 743, 744, 745, 746, 747, 748, 749, 750, 751, 752, 753, 754, 755, 756, 757, 758, 759, 760, 761, 762, 763, 764, 765, 766, 767, 768, 769, 770, 771, 772, 773, 774, 775, 776, 777, 778, 779, 780, 781, 782, 783, 784, 785, 786, 787, 788, 789, 790, 791, 792, 793, 794, 795, 796, 797, 798, 799, 800, 801, 802, 803, 804, 805, 806, 807, 808, 809, 810, 811, 812, 813, 814, 815, 816, 817, 818, 819, 820, 821, 822, 823, 824, 825, 826, 827, 828, 829, 830, 831, 832, 833, 834, 835, 836, 837, 838, 839, 840, 841, 842, 843, 844, 845, 846, 847, 848, 849, 850, 851, 852, 853, 854, 855, 856, 857, 858, 859, 860, 861, 862, 863, 864, 865, 866, 867, 868, 869, 870, 871, 872, 873, 874, 875, 876, 877, 878, 879, 880, 881, 882, 883, 884, 885, 886, 887, 888, 889, 890, 891, 892, 893, 894, 895, 896, 897, 898, 899, 900, 901, 902, 903, 904, 905, 906, 907, 908, 909, 910, 911, 912, 913, 914, 915, 916, 917, 918, 919, 920, 921, 922, 923, 924, 925, 926, 927, 928, 929, 930, 931, 932, 933, 934, 935, 936, 937, 938, 939, 940, 941, 942, 943, 944, 945, 946, 947, 948, 949, 950, 951, 952, 953, 954, 955, 956, 957, 958, 959))

```java
package co.uk.mommyheather.advancedbackups.cli;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.file.FileSystem;
import java.nio.file.FileSystems;
import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.StandardCopyOption;
import java.nio.file.attribute.BasicFileAttributes;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.InputMismatchException;
import java.util.List;
import java.util.Properties;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

import org.fusesource.jansi.AnsiConsole;

import co.uk.mommyheather.advancedbackups.core.ABCore;

public class AdvancedBackupsCLI {

    private static String backupLocation;
    private static File serverDir = new File(new File("").toPath().toAbsolutePath().getParent().toString());
    private static String type;
    private static ArrayList<String> fileNames = new ArrayList<>();
    private static File worldFile;
    private static String worldPath;
    public static void main(String args[]) throws IllegalBackupException {
        
        

        if (System.console() != null) {
            AnsiConsole.systemInstall(); //this gets ansi escape codes working on windows. this was a FUCKING PAIN IN MY ASS
        }
        
        System.out.print("\033[H\033[2J");
        System.out.flush();
        
         
        CLIIOHelpers.info("Advanced Backups - Version " + AdvancedBackupsCLI.class.getPackage().getImplementationVersion());
        CLIIOHelpers.info("Note : this cannot restore backups made prior to the 3.0 release.");
        CLIIOHelpers.info("Searching for properties...", false);

        
        Properties props = new Properties();
        File file = new File(serverDir, "config/AdvancedBackups.properties");
        FileReader reader;
        try {
            reader = new FileReader(file);   
            props.load(reader);

            backupLocation = props.getProperty("config.advancedbackups.path");
            type = props.getProperty("config.advancedbackups.type");
        } catch (Exception e) {
            CLIIOHelpers.error("ERROR LOADING PROPERTIES!");
            CLIIOHelpers.error(getStackTrace(e));
            CLIIOHelpers.error("");
            CLIIOHelpers.error("");
            CLIIOHelpers.error("Ensure you're running this from within the mods directory, and the config file is in the parent directory!");
            // Fatal, cannot proceed
            return;
        }

        if (backupLocation == null || type == null) {
            CLIIOHelpers.error("ERROR LOADING PROPERTIES!");
            CLIIOHelpers.error("Backup location : " + backupLocation);
            CLIIOHelpers.error("Type : " + type);
            // Fatal, cannot proceed
            return;
        }

        CLIIOHelpers.info("Config loaded!");

        //What's a good way to check between absolute and relative? Not all absolute paths will start with /..
        File backupDir = new File(serverDir, backupLocation.replaceAll(Pattern.quote("." + File.separator), ""));

        if (!backupDir.exists()) {
            //Is it absolute?
            backupDir = new File(backupLocation.replaceAll(Pattern.quote("." + File.separator), ""));
            if (!backupDir.exists()) {
                CLIIOHelpers.error("Could not find backup directory!");
                CLIIOHelpers.error(backupDir.getAbsolutePath());
                CLIIOHelpers.error("Have you made any backups before?");
                //Fatal, cannot continue
                return;
            }
        }

        
        //check for backups from "other mods"
        boolean flag = false;
        ArrayList<File> otherBackups = new ArrayList<>();
        for (File b : backupDir.listFiles()) {
            if (b.getName().endsWith("zip")) {
                flag = true;
                otherBackups.add(b);
            }

        }

        if (flag) {
            String result = CLIIOHelpers.getSelectionFromList("Backups from another mod have been found. These can be restored if you want.\nWould you want to work with these backups?", 
            Arrays.asList(new String[]{"Use backups from AdvancedBackups", "Use backups from other mod"}));
            if (result == "Use backups from other mod") {
                restoreOtherModZip(backupDir);
                return;
            }
        }

        type = CLIIOHelpers.getBackupType(type);
        if (type.equals("snapshot (command-made only)")) type = "snapshots";

        /*/
        if (backupLocation.startsWith(Pattern.quote(File.separator)) || backupLocation.indexOf(":") == 1) {
            backupDir = new File(backupLocation, File.separator + type + File.separator);
        }
        else {
            backupDir = new File(serverDir, backupLocation.replaceAll(Pattern.quote("." + File.separator), "") + File.separator + type + File.separator);
        }
*/
        
               
        boolean exportMode = false;
        int backupDateIndex;

        

        CLIIOHelpers.info("Do you want to export a backup, restore the entire world state at this point, or a singular file?");

        String restore = CLIIOHelpers.getSelectionFromList("Enter a number.",
            Arrays.asList(new String[]{"Export backup as zip", "Restore single file", "Restore entire world"}));
        
            
        if (restore.equals("Export backup as zip")) {
            exportMode = true;
        }
        
        
        worldFile = CLIIOHelpers.getWorldFile(serverDir);
        worldPath = worldFile.getName().replace(" ", "_");

        backupDir = new File(backupDir, worldFile.getName() + "/" + type);

        try {
            backupDateIndex = getBackupDate(backupDir, exportMode);
        } catch (IOException e) {
            CLIIOHelpers.error("ERROR VIEWING BACKUPS!");
            e.printStackTrace();
            return;
        }

        if (exportMode) { 
            worldFile = new File(serverDir, "AdvancedBackups.temp");
            worldFile.mkdirs();
        }


        if (!CLIIOHelpers.confirmWarningMessage()) {
            CLIIOHelpers.error("ABORTED - WILL NOT PROCEED.");
            return;
        }

        CLIIOHelpers.info("Preparing...");
        


        switch(restore) {
            case "Restore entire world" : {
                //No going back now!
                CLIIOHelpers.info("Backing up current world state...");
                CLIIOHelpers.info("Backup saved to : " + deleteEntireWorld(worldFile, false));
                switch(type) {
                    case "snapshots" :
                    case "zips" : { 
                        restoreFullZip(backupDateIndex, worldFile);
                        return;
                    }
                    case "differential" : {
                        restoreFullDifferential(backupDateIndex, worldFile);
                        return;
                    }
                    case "incremental" : {
                        restoreFullIncremental(backupDateIndex, worldFile);
                        return;
                    }
                }
            }
            case "Restore single file" : {
                switch(type) {
                    case "snapshots" :
                    case "zips" : {
                        restorePartialZip(backupDateIndex, worldFile);
                        return;
                    }
                    case "differential" : {
                        restorePartialDifferential(backupDateIndex, worldFile);
                        return;
                    }
                    case "incremental" : {
                        restorePartialIncremental(backupDateIndex, worldFile);
                        return;
                    }
                }
            }
            case "Export backup as zip" : {

                CLIIOHelpers.info("Restoring to temporary directory...");

                switch(type) {
                    case "snapshots" :
                    case "zips" : { 
                        restoreFullZip(backupDateIndex, worldFile);
                        break;
                    }
                    case "differential" : {
                        restoreFullDifferential(backupDateIndex, worldFile);
                        break;
                    }
                    case "incremental" : {
                        restoreFullIncremental(backupDateIndex, worldFile);
                        break;
                    }
                }

                CLIIOHelpers.info("Done. Preparing to write to zip...");
                CLIIOHelpers.info("Export saved to : " + deleteEntireWorld(worldFile, true));
            }

        }
    }


    private static String getStackTrace(final Throwable throwable) {
        final StringWriter sw = new StringWriter();
        final PrintWriter pw = new PrintWriter(sw, true);
        throwable.printStackTrace(pw);
        return sw.getBuffer().toString();
    }



    private static int getBackupDate(File backupDir, boolean exportMode) throws IOException {
        fileNames.clear();
        int inputType;

        CLIIOHelpers.info("Select a backup to restore.");

        String[] fileNameArray = backupDir.list();
        if (fileNameArray == null || fileNameArray.length <=0) {
            throw new IOException(String.format("Selected backup directory %s is empty, or is a file!", backupDir.getAbsolutePath()));
        }
        ArrayList<String> fileNameList = new ArrayList<String>(Arrays.asList(fileNameArray)); //i need to do this. i hate this.
        fileNameList.removeIf((name) -> {
            return (name.endsWith("json") ||
                name.contains("incomplete") ||
                name.contains("DS_Store"));
        });

        if (fileNameList.isEmpty()) {
            throw new IOException(String.format("Selected backup directory %s is empty, or is a file!", backupDir.getAbsolutePath()));
        }

        for (String fileName : CLIIOHelpers.sortStringsAlphabeticallyWithDirectoryPriority(fileNameList)) {
            if (exportMode) {
                if (fileName.endsWith("json")) continue;
                if (fileName.contains("incomplete")) continue;
                File file = new File(backupDir, fileName);
                fileNames.add(file.getAbsolutePath());
                String out = file.getName();
                String[] outs = out.split("\\_");
                if (outs.length >=2) {
                    out = ". " + outs[outs.length-2] + "_" + outs[outs.length-1];
                }
                else {
                    out = ". " + out;
                }
                CLIIOHelpers.info(fileNames.size() + out);

            }
            else {
                if (fileName.endsWith("json")) continue;
                if (fileName.contains("incomplete")) continue;
                File file = new File(backupDir, fileName);
                fileNames.add(file.getAbsolutePath());
                String out = file.getName();
                out = out.replaceAll(".zip", "");
                //out = out.replaceAll(worldPath + "_", ": ");
                out = out.replaceAll("backup_", ": ");
                out = out.replaceAll("-partial", "\u001B[33m partial\u001B[0m");
                out = out.replaceAll("-full", "\u001B[32m full\u001B[0m");
                CLIIOHelpers.info(fileNames.size() + out);

            }
        }

        try {
            String line = CLIIOHelpers.input.nextLine();
            if (line == "") {
                CLIIOHelpers.warn("Please enter a number.");
                return getBackupDate(backupDir, exportMode);
            }
            inputType = Integer.parseInt(line);
        } catch (InputMismatchException | NumberFormatException e) {
            CLIIOHelpers.warn("That was not a number. Please enter a number.");
            return getBackupDate(backupDir, exportMode);
        }

        if (inputType < 1 || inputType > fileNames.size()) {
            CLIIOHelpers.warn("Please enter a number between " + fileNames.size() + ".");
            return getBackupDate(backupDir, exportMode);
        }
        
        return inputType - 1;
    }



    private static void restoreFullZip(int index, File worldFile) throws IllegalBackupException {
        byte[] buffer = new byte[1024];
        //The most basic of the bunch.
        ZipEntry entry;
        try {
            FileInputStream fileInputStream = new FileInputStream(fileNames.get(index));
            ZipInputStream zip = new ZipInputStream(fileInputStream);
            while ((entry = zip.getNextEntry()) != null) {
                File outputFile;
                

                outputFile = new File(worldFile, entry.getName());

                if (!outputFile.toPath().normalize().startsWith(worldFile.toPath())) {
                    zip.close();
                    ABCore.errorLogger.accept("Found a potentially malicious zip file - cowardly exiting, restoration may be incomplete!");
                    throw new IllegalBackupException("Zip file is likely malicious! Found an erroneus path: " + entry.getName());
                }
                
                if (!outputFile.getParentFile().exists()) {
                    outputFile.getParentFile().mkdirs();
                }

                CLIIOHelpers.info("Restoring " + outputFile.getName());

                FileOutputStream outputStream = new FileOutputStream(outputFile);
                int length = 0;
                while ((length = zip.read(buffer)) > 0) {
                    outputStream.write(buffer, 0, length);
                }
                outputStream.close();
            }
            zip.close();
        } catch (IOException e) {
            // TODO Auto-generated catch block
            e.printStackTrace();
        }
    }

    private static void restoreFullDifferential(int index, File worldFile) throws IllegalBackupException {
        //Do we need to check for past backups? if selected is a full backup, we do not.
        File backup = new File(fileNames.get(index));
        if (backup.getName().contains("-full")) {
            if (backup.isFile()) {
                restoreFullZip(index, worldFile);
                return;
            }
            restoreFolder(index, worldFile);
            return;
        }
        //find last FULL backup
        for (int i = index;i>=0;i--) {
            String name = fileNames.get(i);
            if (name.contains("-full")) {
                CLIIOHelpers.info("Restoring last full backup...");
                File file = new File(name);
                if (file.isFile()) {
                    restoreFullZip(i, worldFile);
                }
                else {
                    restoreFolder(i, worldFile);
                }
                break;
            }
        }
        CLIIOHelpers.info("\n\nRestoring selected backup...");
        if (backup.isFile()) {
            restoreFullZip(index, worldFile);
        }
        else {
            restoreFolder(index, worldFile);
        }
    }

    private static void restoreFullIncremental(int index, File worldFile) throws IllegalBackupException {
        //Do we need to check for past backups? if selected is a full backup, we do not.
        File backup = new File(fileNames.get(index));
        if (backup.getName().contains("-full")) {
            if (backup.isFile()) {
                restoreFullZip(index, worldFile);
                return;
            }
            restoreFolder(index, worldFile);
            return;
        }
        //find last FULL backup
        int i = index;
        while(i >= 0) {
            String name = fileNames.get(i);
            if (name.contains("-full")) {
                CLIIOHelpers.info("Restoring last full backup...");
                File file = new File(name);
                if (file.isFile()) {
                    restoreFullZip(i, worldFile);
                }
                else {
                    restoreFolder(i, worldFile);
                }
                break;
            }
            i--;
        }
        //restore backups up until the selected one
        while(i < index) {
            String name = fileNames.get(i);
            CLIIOHelpers.info("Restoring chained backup...");
            File file = new File(name);
            if (file.isFile()) {
                restoreFullZip(i, worldFile);
            }
            else {
                restoreFolder(i, worldFile);
            }
            i++;
        }
        
        
        CLIIOHelpers.info("\n\nRestoring selected backup...");
        if (backup.isFile()) {
            restoreFullZip(index, worldFile);
        }
        else {
            restoreFolder(index, worldFile);
        }
    }

    private static void restorePartialZip(int index, File worldFile) {

        HashMap<String, Object> filePaths = new HashMap<>();
        HashMap<String, String> dates = new HashMap<>();
        HashMap<String, ZipFile> entryOwners = new HashMap<>();
        try {
            File backup = new File(fileNames.get(index));
    
            addBackupNamesToLists(backup, entryOwners, filePaths, dates, "\u001B[32m");

            ZipEntry select = ((ZipEntry) CLIIOHelpers.getFileToRestore(filePaths, "", worldFile));

            File outputFile = new File(worldFile, select.toString());
            FileOutputStream outputStream = new FileOutputStream(outputFile);

            CLIIOHelpers.info("Restoring " + select.toString() + "...");

            byte[] buffer = new byte[1028];
            InputStream inputSteam = entryOwners.get(select.toString()).getInputStream(select);
            int length;
            while ((length = inputSteam.read(buffer, 0, buffer.length)) > 0) {
                outputStream.write(buffer, 0, length);
            }
            outputStream.flush();
            outputStream.close();

        } catch (IOException e) {

        }



         
    }

    private static void restorePartialDifferential(int index, File worldFile) throws IllegalBackupException {
        //Do we need to check for past backups? if selected is a full backup, we do not.
        HashMap<String, Object> filePaths = new HashMap<>();
        HashMap<String, String> dates = new HashMap<>();
        HashMap<String, ZipFile> entryOwners = new HashMap<>();
        try {
            File backup = new File(fileNames.get(index));
            if (!backup.getName().contains("-full")) {
                //find last FULL backup
                for (int i = index;i>=0;i--) {
                    String name = fileNames.get(i);
                    if (name.contains("-full")) {
                        addBackupNamesToLists(new File(name), entryOwners, filePaths, dates, "\u001b[31m");
                        break;
                    }    
                }
            }

            File file = new File(fileNames.get(index));
            addBackupNamesToLists(file, entryOwners, filePaths, dates, "\u001B[32m");

            HashMap<String, Object> properMapping = new HashMap<>();
            for (String date : dates.keySet()) {
                properMapping.put(
                    date + " " + dates.get(date),
                    filePaths.get(date)
                );
            }

            Object select = CLIIOHelpers.getFileToRestore(properMapping, "", worldFile);
            if (select instanceof Path) {
                Path input = (Path) select;


                if (select.toString().replace("\\", "/").contains("-full/")) {
                    select = new File(
                        select.toString().replace("\\", "/")
                        .split("-full/")[1]
                    ).toPath();
                }
                if (select.toString().replace("\\", "/").contains("-partial/")) {
                    select = new File(
                        select.toString().replace("\\", "/")
                        .split("-partial/")[1]
                    ).toPath();
                }
    
                File outputFile = new File(worldFile, select.toString());
                if (!outputFile.getParentFile().exists()) {
                    outputFile.getParentFile().mkdirs();
                }

                if (!outputFile.toPath().normalize().startsWith(worldFile.toPath())) {
                    ABCore.errorLogger.accept("Found a potentially malicious zip file - cowardly exiting, restoration may be incomplete!");
                    throw new IllegalBackupException("Zip file is likely malicious! Found an erroneus path: " + select.toString());
                }

                CLIIOHelpers.info("\n\nRestoring file : " + select);
                Files.copy(input, outputFile.toPath(), StandardCopyOption.REPLACE_EXISTING);
            }
            else if (select instanceof ZipEntry) {
                ZipEntry entry = (ZipEntry) select;

                File outputFile = new File(worldFile, entry.toString());

                if (!outputFile.toPath().normalize().startsWith(worldFile.toPath())) {
                    ABCore.errorLogger.accept("Found a potentially malicious zip file - cowardly exiting, restoration may be incomplete!");
                    throw new IllegalBackupException("Zip file is likely malicious! Found an erroneus path: " + select.toString());
                }

                FileOutputStream outputStream = new FileOutputStream(outputFile);

                CLIIOHelpers.info("Restoring " + entry.toString() + "...");

                byte[] buffer = new byte[1028];
                InputStream inputSteam = entryOwners.get(entry.toString()).getInputStream(entry);
                int length;
                while ((length = inputSteam.read(buffer, 0, buffer.length)) > 0) {
                    outputStream.write(buffer, 0, length);
                }
                outputStream.flush();
                outputStream.close();
            }


        }
        catch (IOException e){
            //TODO : Scream at user
            e.printStackTrace();
        }
    }

    private static void restorePartialIncremental(int index, File worldFile) throws IllegalBackupException {
        //Do we need to check for past backups? if selected is a full backup, we do not.
        HashMap<String, Object> filePaths = new HashMap<>();
        HashMap<String, String> dates = new HashMap<>();
        HashMap<String, ZipFile> entryOwners = new HashMap<>();
        try {
            File backup = new File(fileNames.get(index));
            if (!backup.getName().contains("-full")) {
                int i;
                //find last FULL backup
                for (i = index;i>=0;i--) {
                    String name = fileNames.get(i);
                    if (name.contains("-full")) {
                        addBackupNamesToLists(new File(name), entryOwners, filePaths, dates, "\u001b[31m");
                        break;
                    }    
                }
                while (i < index) {
                    String name = fileNames.get(i);
                    addBackupNamesToLists(new File(name), entryOwners, filePaths, dates, "\u001b[31m");
                    i++;
                }
                
            }

            File file = new File(fileNames.get(index));
            addBackupNamesToLists(file, entryOwners, filePaths, dates, "\u001B[32m");

            HashMap<String, Object> properMapping = new HashMap<>();
            for (String date : dates.keySet()) {
                properMapping.put(
                    date + " " + dates.get(date),
                    filePaths.get(date)
                );
            }

            Object select = CLIIOHelpers.getFileToRestore(properMapping, "", worldFile);
            if (select instanceof Path) {
                Path input = (Path) select;


                if (select.toString().replace("\\", "/").contains("-full/")) {
                    select = new File(
                        select.toString().replace("\\", "/")
                        .split("-full/")[1]
                    ).toPath();
                }
                if (select.toString().replace("\\", "/").contains("-partial/")) {
                    select = new File(
                        select.toString().replace("\\", "/")
                        .split("-partial/")[1]
                    ).toPath();
                }
    
                File outputFile = new File(worldFile, select.toString());
                

                if (!outputFile.toPath().normalize().startsWith(worldFile.toPath())) {
                    ABCore.errorLogger.accept("Found a potentially malicious zip file - cowardly exiting, restoration may be incomplete!");
                    throw new IllegalBackupException("Zip file is likely malicious! Found an erroneus path: " + select.toString());
                }
                
                if (!outputFile.getParentFile().exists()) {
                    outputFile.getParentFile().mkdirs();
                }
                CLIIOHelpers.info("\n\nRestoring file : " + select);
                Files.copy(input, outputFile.toPath(), StandardCopyOption.REPLACE_EXISTING);
            }
            else if (select instanceof ZipEntry) {
                ZipEntry entry = (ZipEntry) select;

                File outputFile = new File(worldFile, entry.toString());
                
                if (!outputFile.toPath().normalize().startsWith(worldFile.toPath())) {
                    ABCore.errorLogger.accept("Found a potentially malicious zip file - cowardly exiting, restoration may be incomplete!");
                    throw new IllegalBackupException("Zip file is likely malicious! Found an erroneus path: " + select.toString());
                }

                FileOutputStream outputStream = new FileOutputStream(outputFile);

                CLIIOHelpers.info("Restoring " + entry.toString() + "...");

                byte[] buffer = new byte[1028];
                InputStream inputSteam = entryOwners.get(entry.toString()).getInputStream(entry);
                int length;
                while ((length = inputSteam.read(buffer, 0, buffer.length)) > 0) {
                    outputStream.write(buffer, 0, length);
                }
                outputStream.flush();
                outputStream.close();
            }


        }
        catch (IOException e){
            //TODO : Scream at user
            e.printStackTrace();
        }
    }


    private static void restoreFolder(int index, File worldFile) throws IllegalBackupException {
        File backup = new File(fileNames.get(index));

        try {
            Files.walkFileTree(backup.toPath(), new SimpleFileVisitor<Path>() {
                @Override
                public FileVisitResult visitFile(Path file, BasicFileAttributes attributes) throws IOException {
                    File source = backup.toPath().relativize(file).toFile();
                    File outputFile = new File(worldFile, source.getPath());

                    if (!outputFile.toPath().normalize().startsWith(worldFile.toPath())) {
                        ABCore.errorLogger.accept("Found a potentially malicious zip file - cowardly exiting, restoration may be incomplete!");
                        new IllegalBackupException("Zip file is likely malicious! Found an erroneus path: " + source.getPath()).printStackTrace();
                        return FileVisitResult.TERMINATE;
                    }

                    if (!outputFile.getParentFile().exists()) {
                        outputFile.getParentFile().mkdirs();
                    }
                    
                    CLIIOHelpers.info("Restoring " + outputFile.getName());
                    Files.copy(file, outputFile.toPath());
                    return FileVisitResult.CONTINUE;
                }
            });
        } catch (IOException e) {
            // TODO Auto-generated catch block
            e.printStackTrace();
        }
    }

    
    private static void restoreOtherModZip(File backupDir) throws IllegalBackupException {
        worldFile = serverDir;
        Path file;
        HashMap<String, File> backups = new HashMap<>();
        HashMap<String, Path> entries = new HashMap<>();

        for (File b : backupDir.getParentFile().listFiles()) {
            if (b.getName().endsWith("zip")) {
                backups.put(b.getName(), b);
            }
        }

        String backupName = CLIIOHelpers.getSelectionFromList("Select a backup to restore from.", new ArrayList<String>(backups.keySet()));
        
        boolean fullWorld = CLIIOHelpers.getSelectionFromList("Do you want to restore the whole world or a singular file?", 
        Arrays.asList(new String[]{"Whole world", "Single file"})) == "Whole world";

        if (!fullWorld) {
            if (!CLIIOHelpers.confirmWarningMessage()) return;
            
            try {
                FileSystem zipFs = FileSystems.newFileSystem(backups.get(backupName).toPath(), AdvancedBackupsCLI.class.getClassLoader());
                Path root = zipFs.getPath("");
                Files.walkFileTree(root, new SimpleFileVisitor<Path>() {
                    @Override
                    public FileVisitResult visitFile(Path file, BasicFileAttributes attributes) throws IOException {
                        entries.put(file.toString(), file);
                        return FileVisitResult.CONTINUE;
                    }
                });

                file = CLIIOHelpers.getFileToRestore(entries, "", worldFile);
                CLIIOHelpers.info("Restoring " + file.toString() + "...");
                Path outputFile = new File(worldFile, file.toString()).toPath();

                if (!outputFile.normalize().startsWith(worldFile.toPath())) {
                    ABCore.errorLogger.accept("Found a potentially malicious zip file - cowardly exiting, restoration may be incomplete!");
                    throw new IllegalBackupException("Zip file is likely malicious! Found an erroneus path: " + file.toString());
                }
                if (!outputFile.getParent().toFile().exists()) {
                    outputFile.getParent().toFile().mkdirs();
                }
                Files.copy(file, outputFile, StandardCopyOption.REPLACE_EXISTING);
                CLIIOHelpers.info("Done.");
                
            } catch (IOException e) {
                // TODO Auto-generated catch block
                e.printStackTrace();
                return;
            }
        }

        else {
            if (!CLIIOHelpers.confirmWarningMessage()) return;

            Path levelDatPath;
            ArrayList<Path> levelDatPathWrapper = new ArrayList<>();
            
            try {
                FileSystem zipFs = FileSystems.newFileSystem(backups.get(backupName).toPath(), AdvancedBackupsCLI.class.getClassLoader());
                Path root = zipFs.getPath("");
                Files.walkFileTree(root, new SimpleFileVisitor<Path>() {
                    @Override
                    public FileVisitResult visitFile(Path file, BasicFileAttributes attributes) throws IOException {
                        if (file.getFileName().toString().equals("level.dat")) levelDatPathWrapper.add(file);
                        return FileVisitResult.CONTINUE;
                    }
                });

                levelDatPath = levelDatPathWrapper.get(0);
                CLIIOHelpers.info("Making backup of existing world...");
                CLIIOHelpers.info("Backup saved to : " + deleteEntireWorld(new File(worldFile, levelDatPath.getParent().toString()), false));
                byte[] buffer = new byte[1024];
                ZipEntry entry;
                FileInputStream fileInputStream = new FileInputStream(backups.get(backupName));
                ZipInputStream zip = new ZipInputStream(fileInputStream);
                while ((entry = zip.getNextEntry()) != null) {
                    File outputFile;
    
                    outputFile = new File(worldFile, entry.getName());

                    if (!outputFile.toPath().normalize().startsWith(worldFile.toPath())) {
                        zip.close();
                        ABCore.errorLogger.accept("Found a potentially malicious zip file - cowardly exiting, restoration may be incomplete!");
                        throw new IllegalBackupException("Zip file is likely malicious! Found an erroneus path: " + entry.getName());
                    }
                    
                    if (!outputFile.getParentFile().exists()) {
                        outputFile.getParentFile().mkdirs();
                    }

                    
                    CLIIOHelpers.info("Restoring " + outputFile.toString() + "...");
    
                    FileOutputStream outputStream = new FileOutputStream(outputFile);
                    int length = 0;
                    while ((length = zip.read(buffer)) > 0) {
                        outputStream.write(buffer, 0, length);
                    }
                    outputStream.close();
                }
                zip.close();
                
            } catch (IOException e) {
                // TODO Auto-generated catch block
                e.printStackTrace();
            }


            
        }

        CLIIOHelpers.info("Done.");

    }



    private static String deleteEntireWorld(File worldDir, boolean exportMode) {
        String ret = backupExistingWorld(worldDir, exportMode);
        try {
            Files.walkFileTree(worldDir.toPath(), new SimpleFileVisitor<Path>() {
                @Override
                public FileVisitResult visitFile(Path file, BasicFileAttributes attributes) {
                    file.toFile().delete();
                    return FileVisitResult.CONTINUE;
                }
                
                @Override
                public FileVisitResult postVisitDirectory(Path file, java.io.IOException arg1) {
                    if (file.toFile().listFiles().length == 0) {
                        file.toFile().delete();
                    }
                    return FileVisitResult.CONTINUE;
                }
            });
        } catch (IOException e) {
            CLIIOHelpers.warn("Failed to delete file :");
            e.printStackTrace();
        }
        return ret;
    }

    private static String backupExistingWorld(File worldDir, boolean export) {
        File out = new File(worldDir, "../cli" + serialiseBackupName(export ? "export" : "backup") + ".zip");
        try {
            FileOutputStream outputStream = new FileOutputStream(out);
            ZipOutputStream zipOutputStream = new ZipOutputStream(outputStream);
            zipOutputStream.setLevel(4);
            Files.walkFileTree(worldDir.toPath(), new SimpleFileVisitor<Path>() {
                @Override
                public FileVisitResult visitFile(Path file, BasicFileAttributes attributes) {
                    Path targetFile;
                    try {
                        targetFile = worldDir.toPath().relativize(file);
                        if (targetFile.toFile().getName().compareTo("session.lock") == 0) {
                            return FileVisitResult.CONTINUE;
                        }
                        zipOutputStream.putNextEntry(new ZipEntry(targetFile.toString()));
                        byte[] bytes = Files.readAllBytes(file);
                        zipOutputStream.write(bytes, 0, bytes.length);
                        zipOutputStream.closeEntry();

                    } catch (IOException e) {
                        // TODO : Scream at user
                        e.printStackTrace();
                    }
                    
                        return FileVisitResult.CONTINUE;
                }
            });
            zipOutputStream.flush();
            zipOutputStream.close();

            CLIIOHelpers.info("Done.");
        } catch (Exception e) {
            
        }

        return out.getName();
    }



    private static void addBackupNamesToLists(File file, HashMap<String, ZipFile> entryOwners, HashMap<String, Object> filePaths, 
        HashMap<String, String> dates, String colour) throws IOException {
        
        if (file.isFile()) {
            
            ZipFile zipFile = new ZipFile(file);
            Enumeration<? extends ZipEntry> entryEnum = zipFile.entries();

            while (entryEnum.hasMoreElements()) {
                ZipEntry entry = entryEnum.nextElement();

                String backupName = file.toString().replace("\\", "/");
                filePaths.put(entry.toString().replace("\\", "/"), entry);
                dates.put(entry.toString().replace("\\", "/"), "\u001b[31m"
                 + backupName
                .substring(backupName.toString().lastIndexOf("/") + 1) 
                //.replace(worldPath + "_", "")
                .replace("backup_", "")
                .replace("-full.zip", "")
                .replace("-partial.zip", "")
                + "\u001B[0m");
                entryOwners.put(entry.toString(), zipFile);
            }
        }

        else {
            Files.walkFileTree(file.toPath(), new SimpleFileVisitor<Path>() {
                @Override
                public FileVisitResult visitFile(Path path, BasicFileAttributes attributes) throws IOException {
                    String backupName = file.toString().replace("\\", "/");
                    filePaths.put(file.toPath().relativize(path).toString().replace("\\", "/"), path);
                    dates.put(file.toPath().relativize(path).toString().replace("\\", "/"), "\u001b[31m"
                     + backupName
                    .substring(backupName.toString().lastIndexOf("/") + 1) 
                    //.replace(worldPath + "_", "")
                    .replace("backup_", "")
                    .replace("-full", "")
                    .replace("-partial", "")
                    + "\u001B[0m");
                    return FileVisitResult.CONTINUE;
                }
            });
        }
    }

    
    public static String serialiseBackupName(String in) {
        Date date = new Date();
        String pattern = "yyyy-MM-dd_HH-mm-ss";
        
        return in + "_" + new SimpleDateFormat(pattern).format(date);
    }
    
}
```

## `src/main/java/co/uk/mommyheather/advancedbackups/cli/IllegalBackupException.java` (changed lines (1, 2, 3, 4, 5, 6, 7, 8, 9))

```java
package co.uk.mommyheather.advancedbackups.cli;

public class IllegalBackupException extends Exception {

    public IllegalBackupException(String string) {
        super(string);
    }
    
}
```
