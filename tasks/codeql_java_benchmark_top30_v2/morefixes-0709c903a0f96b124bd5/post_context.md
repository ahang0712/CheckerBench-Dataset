# Patch-overlapping Java context after the fix

## `src/main/java/io/jenkins/plugins/aws/global_configuration/AwsManagementLink.java` (changed lines (17, 64, 66))

```java
package io.jenkins.plugins.aws.global_configuration;

import com.google.common.base.Predicate;
import hudson.BulkChange;
import hudson.Extension;
import hudson.Functions;
import hudson.model.Describable;
import hudson.model.Descriptor;
import hudson.model.ManagementLink;
import hudson.util.FormApply;
import jenkins.model.GlobalConfigurationCategory;
import jenkins.model.Jenkins;
import net.sf.json.JSONObject;
import org.jenkinsci.Symbol;
import org.kohsuke.stapler.StaplerRequest;
import org.kohsuke.stapler.StaplerResponse;
import org.kohsuke.stapler.verb.POST;

import javax.annotation.CheckForNull;
import javax.servlet.ServletException;
import java.io.IOException;
import java.util.logging.Level;
import java.util.logging.Logger;

/**
 * @author Oleg Nenashev
 * @since TODO
 */
@Extension @Symbol("aws")
public class AwsManagementLink extends ManagementLink implements Describable<AwsManagementLink> {

    private static Logger LOGGER = Logger.getLogger(AwsManagementLink.class.getName());

    @CheckForNull
    @Override
    public String getUrlName() {
        return "aws";
    }

    @CheckForNull
    @Override
    public String getDisplayName() {
        return "AWS";
    }

    @Override
    public String getIconFileName() {
        return "/plugin/aws-global-configuration/images/aws.png";
    }

    @Override
    public String getDescription() {
        return "Amazon Web Services Configuration";
    }

    @Override
    public Category getCategory() { return Category.CONFIGURATION; }

    @Override
    public Descriptor<AwsManagementLink> getDescriptor() {
        return Jenkins.get().getDescriptorOrDie(AwsManagementLink.class);
    }

    @POST
    public synchronized void doConfigure(StaplerRequest req, StaplerResponse rsp) throws IOException, ServletException, Descriptor.FormException {
        Jenkins.get().checkPermission(Jenkins.ADMINISTER);
        // for compatibility reasons, the actual value is stored in Jenkins
        BulkChange bc = new BulkChange(Jenkins.get());
        try{
            boolean result = configure(req, req.getSubmittedForm());
            LOGGER.log(Level.FINE, "security saved: "+result);
            Jenkins.get().save();
            FormApply.success(req.getContextPath()+"/manage").generateResponse(req, rsp, null);
        } finally {
            bc.commit();
        }
    }

    public boolean configure(StaplerRequest req, JSONObject json) throws Descriptor.FormException {
        boolean result = true;
        for(Descriptor<?> d : Functions.getSortedDescriptorsForGlobalConfigByDescriptor(descriptor ->
                FILTER.apply(descriptor.getCategory()))){
            result &= configureDescriptor(req,json,d);
        }

        return result;
    }

    private boolean configureDescriptor(StaplerRequest req, JSONObject json, Descriptor<?> d) throws Descriptor.FormException {
        // collapse the structure to remain backward compatible with the JSON structure before 1.
        String name = d.getJsonSafeClassName();
        JSONObject js = json.has(name) ? json.getJSONObject(name) : new JSONObject(); // if it doesn't have the property, the method returns invalid null object.
        json.putAll(js);
        return d.configure(req, js);
    }

    public static final Predicate<GlobalConfigurationCategory> FILTER = new Predicate<GlobalConfigurationCategory>() {
        public boolean apply(GlobalConfigurationCategory input) {
            return input instanceof AwsGlobalConfigurationCategory;
        }
    };

    @Extension
    @Symbol("aws")
    public static final class DescriptorImpl extends Descriptor<AwsManagementLink> {
        @Override
        public String getDisplayName() {
            return "AWS";
        }
    }
}
```
