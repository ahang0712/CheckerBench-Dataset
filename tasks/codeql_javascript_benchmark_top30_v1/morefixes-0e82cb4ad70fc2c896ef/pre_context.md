# Patch-overlapping JavaScript context before the fix

## `lib/common.js` (changed lines (38,))

```javascript
const _ = require('lodash');
const uglifycss = require('uglifycss');
const colors = require('colors');
const lunr = require('lunr');
const cheerio = require('cheerio');
const fs = require('fs');
const path = require('path');
const glob = require('glob');
const async = require('async');
const nodemailer = require('nodemailer');
const escape = require('html-entities').AllHtmlEntities;
let ObjectId = require('mongodb').ObjectID;

const restrictedRoutes = [
    {route: '/admin/product/new', response: 'redirect'},
    {route: '/admin/product/insert', response: 'redirect'},
    {route: '/admin/product/edit/:id', response: 'redirect'},
    {route: '/admin/product/update', response: 'redirect'},
    {route: '/admin/product/delete/:id', response: 'redirect'},
    {route: '/admin/product/published_state', response: 'json'},
    {route: '/admin/product/setasmainimage', response: 'json'},
    {route: '/admin/product/deleteimage', response: 'json'},
    {route: '/admin/order/statusupdate', response: 'json'},
    {route: '/admin/settings/update', response: 'json'},
    {route: '/admin/settings/option/remove', response: 'json'},
    {route: '/admin/settings/pages/new', response: 'redirect'},
    {route: '/admin/settings/pages/edit/:page', response: 'redirect'},
    {route: '/admin/settings/pages/update', response: 'json'},
    {route: '/admin/settings/pages/delete/:page', response: 'redirect'},
    {route: '/admin/settings/menu/new', response: 'redirect'},
    {route: '/admin/settings/menu/update', response: 'redirect'},
    {route: '/admin/settings/menu/delete/:menuid', response: 'redirect'},
    {route: '/admin/settings/menu/save_order', response: 'json'},
    {route: '/admin/file/upload', response: 'redirect'},
    {route: '/admin/file/delete', response: 'json'}
];

// common functions
exports.restrict = (req, res, next) => {
    exports.checkLogin(req, res, next);
};

exports.checkLogin = (req, res, next) => {
    // if not protecting we check for public pages and don't checkLogin
    if(req.session.needsSetup === true){
        res.redirect('/admin/setup');
        return;
    }

    if(req.session.user){
        next();
        return;
    }
    res.redirect('/admin/login');
};

// Middleware to check for admin access for certain route
exports.checkAccess = (req, res, next) => {
    const routeCheck = _.find(restrictedRoutes, {'route': req.route.path});

    // If the user is not an admin and route is restricted, show message and redirect to /admin
    if(req.session.isAdmin === false && routeCheck){
        if(routeCheck.response === 'redirect'){
            req.session.message = 'Unauthorised. Please refer to administrator.';
            req.session.messageType = 'danger';
            res.redirect('/admin');
            return;
        }
        if(routeCheck.response === 'json'){
            res.status(400).json({message: 'Unauthorised. Please refer to administrator.'});
        }
    }else{
        next();
    }
};

exports.showCartCloseBtn = (page) => {
    let showCartCloseButton = true;
    if(page === 'checkout' || page === 'pay'){
        showCartCloseButton = false;
    }

    return showCartCloseButton;
};

// adds products to sitemap.xml
exports.addSitemapProducts = (req, res, cb) => {
    let db = req.app.db;

    let config = exports.getConfig();
    let hostname = config.baseUrl;

    db.products.find({productPublished: 'true'}).toArray((err, products) => {
        let posts = [];
        if(err){
            cb(null, posts);
        }
        async.eachSeries(products, (item, callback) => {
            let post = {};
            let url = item._id;
            if(item.productPermalink){
                url = item.productPermalink;
            }
            post.url = hostname + '/' + url;
            post.changefreq = 'weekly';
            post.priority = 0.7;
            posts.push(post);
            callback(null, posts);
        }, () => {
            cb(null, posts);
        });
    });
};

exports.clearSessionValue = (session, sessionVar) => {
    let temp;
    if(session){
        temp = session[sessionVar];
        session[sessionVar] = null;
    }
    return temp;
};

exports.updateTotalCartAmount = (req, res) => {
    let config = exports.getConfig();

    req.session.totalCartAmount = 0;

    _(req.session.cart).forEach((item) => {
        req.session.totalCartAmount = req.session.totalCartAmount + item.totalItemPrice;
    });

    // under the free shipping threshold
    if(req.session.totalCartAmount < config.freeShippingAmount){
        req.session.totalCartAmount = req.session.totalCartAmount + parseInt(config.flatShipping);
        req.session.shippingCostApplied = true;
    }else{
        req.session.shippingCostApplied = false;
    }
};

exports.checkDirectorySync = (directory) => {
    try{
        fs.statSync(directory);
    }catch(e){
        fs.mkdirSync(directory);
    }
};

exports.getThemes = () => {
    return fs.readdirSync(path.join(__dirname, '../', 'views', 'themes')).filter(file => fs.statSync(path.join(path.join(__dirname, '../', 'views', 'themes'), file)).isDirectory());
};

exports.getImages = (dir, req, res, callback) => {
    let db = req.app.db;

    db.products.findOne({_id: exports.getId(dir)}, (err, product) => {
        if(err){
            console.error(colors.red('Error getting images', err));
        }

        // loop files in /public/uploads/
        glob('public/uploads/' + product.productPermalink + '/**', {nosort: true}, (er, files) => {
            // sort array
            files.sort();

            // declare the array of objects
            let fileList = [];

            // loop these files
            for(let i = 0; i < files.length; i++){
                // only want files
                if(fs.lstatSync(files[i]).isDirectory() === false){
                    // declare the file object and set its values
                    let file = {
                        id: i,
                        path: files[i].substring(6)
                    };
                    if(product.productImage === files[i].substring(6)){
                        file.productImage = true;
                    }
                    // push the file object into the array
                    fileList.push(file);
                }
            }
            callback(fileList);
        });
    });
};

exports.getConfig = () => {
    let config = JSON.parse(fs.readFileSync(path.join(__dirname, '../config', 'settings.json'), 'utf8'));
    config.customCss = typeof config.customCss !== 'undefined' ? escape.decode(config.customCss) : null;
    config.footerHtml = typeof config.footerHtml !== 'undefined' ? escape.decode(config.footerHtml) : null;
    config.googleAnalytics = typeof config.googleAnalytics !== 'undefined' ? escape.decode(config.googleAnalytics) : null;

    // set the environment for files
    config.env = '.min';
    if(process.env.NODE_ENV === 'development' || process.env.NODE_ENV === undefined){
        config.env = '';
    }

    // setup theme
    config.themeViews = '';
    if(typeof config.theme === 'undefined' || config.theme === ''){
        config.theme = 'Cloth'; // Default to Cloth theme
    }

    config.themeViews = '../views/themes/' + config.theme + '/';

    // if db set to mongodb override connection with MONGODB_CONNECTION_STRING env var
    config.databaseConnectionString = process.env.MONGODB_CONNECTION_STRING || config.databaseConnectionString;

    return config;
};

exports.getPaymentConfig = () => {
    let siteConfig = this.getConfig();

    let config = [];
    if(fs.existsSync(path.join(__dirname, '../config/' + siteConfig.paymentGateway + '.json'))){
        config = JSON.parse(fs.readFileSync(path.join(__dirname, '../config/' + siteConfig.paymentGateway + '.json'), 'utf8'));
    }

    return config;
};

exports.updateConfig = (fields) => {
    let settingsFile = JSON.parse(fs.readFileSync(path.join(__dirname, '../config/settings.json'), 'utf8'));

    _.forEach(fields, (value, key) => {
        settingsFile[key] = value;
        if(key === 'customCss_input'){
            settingsFile['customCss'] = escape.encode(uglifycss.processString(value));
        }
        if(key === 'footerHtml_input'){
            let footerHtml = typeof value !== 'undefined' || value === '' ? escape.encode(value) : '';
            settingsFile['footerHtml'] = footerHtml;
        }
        if(key === 'googleAnalytics_input'){
            let googleAnalytics = typeof value !== 'undefined' ? escape.encode(value) : '';
            settingsFile['googleAnalytics'] = googleAnalytics;
        }
    });

    // delete settings
    delete settingsFile['customCss_input'];
    delete settingsFile['footerHtml_input'];
    delete settingsFile['googleAnalytics_input'];

    if(fields['emailSecure'] === 'on'){
        settingsFile['emailSecure'] = true;
    }else{
        settingsFile['emailSecure'] = false;
    }

    if(!fields['menuEnabled']){
        settingsFile['menuEnabled'] = false;
    }else{
        settingsFile['menuEnabled'] = true;
    }

    if(fields['emailPort']){
        settingsFile['emailPort'] = parseInt(fields['emailPort']);
    }

    if(fields['flatShipping']){
        settingsFile['flatShipping'] = parseInt(fields['flatShipping']);
    }

    if(fields['freeShippingAmount']){
        settingsFile['freeShippingAmount'] = parseInt(fields['freeShippingAmount']);
    }

    if(fields['productsPerRow']){
        settingsFile['productsPerRow'] = parseInt(fields['productsPerRow']);
    }

    if(fields['productsPerPage']){
        settingsFile['productsPerPage'] = parseInt(fields['productsPerPage']);
    }

    // write file
    try{
        fs.writeFileSync(path.join(__dirname, '../config/settings.json'), JSON.stringify(settingsFile, null, 4));
        return true;
    }catch(exception){
        return false;
    }
};

exports.getMenu = (db) => {
    return db.menu.findOne({});
};

// creates a new menu item
exports.newMenu = (req, res) => {
    const db = req.app.db;
    return exports.getMenu(db)
    .then((menu) => {
        // if no menu present
        if(!menu){
            menu = {};
            menu.items = [];
        }
        let newNav = {
            title: req.body.navMenu,
            link: req.body.navLink,
            order: Object.keys(menu.items).length + 1
        };

        menu.items.push(newNav);
        return db.menu.updateOne({}, {$set: {items: menu.items}}, {upsert: true})
        .then(() => {
            return true;
        });
    })
    .catch((err) => {
        console.log('Error creating new menu', err);
        return false;
    });
};

// delete a menu item
exports.deleteMenu = (req, res, menuIndex) => {
    const db = req.app.db;
    return exports.getMenu(db)
    .then((menu) => {
        // Remove menu item
        menu.items.splice(menuIndex, 1);
        return db.menu.updateOne({}, {$set: {items: menu.items}}, {upsert: true})
        .then(() => {
            return true;
        });
    })
    .catch(() => {
        return false;
    });
};

// updates and existing menu item
exports.updateMenu = (req, res) => {
    const db = req.app.db;
    return exports.getMenu(db)
    .then((menu) => {
        // find menu item and update it
        let menuIndex = _.findIndex(menu.items, ['title', req.body.navId]);
        menu.items[menuIndex].title = req.body.navMenu;
        menu.items[menuIndex].link = req.body.navLink;
        return db.menu.updateOne({}, {$set: {items: menu.items}}, {upsert: true})
        .then(() => {
            return true;
        });
    })
    .catch(() => {
        return false;
    });
};

exports.sortMenu = (menu) => {
    if(menu && menu.items){
        menu.items = _.sortBy(menu.items, 'order');
        return menu;
    }
    return{};
};

// orders the menu
exports.orderMenu = (req, res) => {
    const db = req.app.db;
    return exports.getMenu(db)
    .then((menu) => {
        // update the order
        for(let i = 0; i < req.body.navId.length; i++){
            _.find(menu.items, ['title', req.body.navId[i]]).order = i;
        }
        return db.menu.updateOne({}, {$set: {items: menu.items}}, {upsert: true})
        .then(() => {
            return true;
        });
    })
    .catch(() => {
        return false;
    });
};

exports.getEmailTemplate = (result) => {
    let config = this.getConfig();

    let template = fs.readFileSync(path.join(__dirname, '../public/email_template.html'), 'utf8');

    $ = cheerio.load(template);
    $('#brand').text(config.cartTitle);
    $('#paymentResult').text(result.message);
    if(result.paymentApproved === true){
        $('#paymentResult').addClass('text-success');
    }else{
        $('#paymentResult').addClass('text-danger');
    }
    $('#paymentMessage').text('Thanks for shopping with us. We hope you will shop with us again soon.');
    $('#paymentDetails').html(result.paymentDetails);

    return $.html();
};

exports.sendEmail = (to, subject, body) => {
    let config = this.getConfig();

    let emailSettings = {
        host: config.emailHost,
        port: config.emailPort,
        secure: config.emailSecure,
        auth: {
            user: config.emailUser,
            pass: config.emailPassword
        }
    };

    // outlook needs this setting
    if(config.emailHost === 'smtp-mail.outlook.com'){
        emailSettings.tls = {ciphers: 'SSLv3'};
    }

    let transporter = nodemailer.createTransport(emailSettings);

    let mailOptions = {
        from: config.emailAddress, // sender address
        to: to, // list of receivers
        subject: subject, // Subject line
        html: body// html body
    };

    transporter.sendMail(mailOptions, (error, info) => {
        if(error){
            return console.error(colors.red(error));
        }
        return true;
    });
};

// gets the correct type of index ID
exports.getId = (id) => {
    if(id){
        if(id.length !== 24){
            return id;
        }
    }
    return ObjectId(id);
};

exports.getData = (req, page, query) => {
    let db = req.app.db;
    let config = exports.getConfig();
    let numberProducts = config.productsPerPage ? config.productsPerPage : 6;

    let skip = 0;
    if(page > 1){
        skip = (page - 1) * numberProducts;
    }

    if(!query){
        query = {};
    }

    query['productPublished'] = 'true';

    // Run our queries
    return Promise.all([
        db.products.find(query).skip(skip).limit(parseInt(numberProducts)).toArray(),
        db.products.count(query)
    ])
    .then((result) => {
        const returnData = {data: result[0], totalProducts: result[1]};
        return returnData;
    })
    .catch((err) => {
        throw new Error('Error retrieving products');
    });
};

exports.indexProducts = (app) => {
    // index all products in lunr on startup
    return new Promise((resolve, reject) => {
        app.db.products.find({}).toArray((err, productsList) => {
            if(err){
                console.error(colors.red(err.stack));
                reject(err);
            }

            // setup lunr indexing
            const productsIndex = lunr(function(){
                this.field('productTitle', {boost: 10});
                this.field('productTags', {boost: 5});
                this.field('productDescription');

                const lunrIndex = this;

                // add to lunr index
                productsList.forEach((product) => {
                    let doc = {
                        'productTitle': product.productTitle,
                        'productTags': product.productTags,
                        'productDescription': product.productDescription,
                        'id': product._id
                    };
                    lunrIndex.add(doc);
                });
            });

            app.productsIndex = productsIndex;
            console.log(colors.cyan('- Product indexing complete'));
            resolve();
        });
    });
};

exports.indexCustomers = (app) => {
    // index all products in lunr on startup
    return new Promise((resolve, reject) => {
        app.db.customers.find({}).toArray((err, customerList) => {
            if(err){
                console.error(colors.red(err.stack));
                reject(err);
            }

            // setup lunr indexing
            const customersIndex = lunr(function(){
                this.field('email', {boost: 10});
                this.field('name', {boost: 5});
                this.field('phone');

                const lunrIndex = this;

                // add to lunr index
                customerList.forEach((customer) => {
                    let doc = {
                        'email': customer.email,
                        'name': `${customer.firstName} ${customer.lastName}`,
                        'phone': customer.phone,
                        'id': customer._id
                    };
                    lunrIndex.add(doc);
                });
            });

            app.customersIndex = customersIndex;
            console.log(colors.cyan('- Customer indexing complete'));
            resolve();
        });
    });
};

exports.indexOrders = (app, cb) => {
    // index all orders in lunr on startup
    return new Promise((resolve, reject) => {
        app.db.orders.find({}).toArray((err, ordersList) => {
            if(err){
                console.error(colors.red('Error setting up products index: ' + err));
                reject(err);
            }

            // setup lunr indexing
            const ordersIndex = lunr(function(){
                this.field('orderEmail', {boost: 10});
                this.field('orderLastname', {boost: 5});
                this.field('orderPostcode');

                const lunrIndex = this;

                // add to lunr index
                ordersList.forEach((order) => {
                    let doc = {
                        'orderLastname': order.orderLastname,
                        'orderEmail': order.orderEmail,
                        'orderPostcode': order.orderPostcode,
                        'id': order._id
                    };
                    lunrIndex.add(doc);
                });
            });

            app.ordersIndex = ordersIndex;
            console.log(colors.cyan('- Order indexing complete'));
            resolve();
        });
    });
};

// start indexing products and orders
exports.runIndexing = (app) => {
    console.info(colors.yellow('Setting up indexes..'));

    return Promise.all([
        exports.indexProducts(app),
        exports.indexOrders(app),
        exports.indexCustomers(app)
    ])
    .catch((err) => {
        process.exit(2);
    });
};

exports.dropTestData = (db) => {
    Promise.all([
        db.products.drop(),
        db.users.drop(),
        db.customers.drop()
    ])
    .then((err) => {
        return Promise.resolve();
    })
    .catch((err) => {
        console.log('Error dropping test data', err);
    });
};

exports.sampleData = (app) => {
    const db = app.db;

    db.products.count()
    .then((products) => {
        if(products !== 0){
            return Promise.resolve();
        }

        console.log('Inserting sample data');
        const testData = fs.readFileSync('./bin/testdata.json', 'utf-8');
        const jsonData = JSON.parse(testData);

        // Add sample data
        return Promise.all([
            db.products.insertMany(fixProductDates(jsonData.products)),
            db.menu.insertOne(jsonData.menu)
        ]);
    });
};

exports.testData = async (app) => {
    const db = app.db;
    const testData = fs.readFileSync('./bin/testdata.json', 'utf-8');
    const jsonData = JSON.parse(testData);

    // TODO: A bit ugly, needs fixing
    return new Promise((resolve, reject) => {
        Promise.all([
            db.users.remove({}, {}),
            db.customers.remove({}, {}),
            db.products.remove({}, {}),
            db.menu.remove({}, {})
        ])
        .then(() => {
            Promise.all([
                db.users.insertMany(jsonData.users),
                db.customers.insertMany(jsonData.customers),
                db.products.insertMany(fixProductDates(jsonData.products)),
                db.menu.insertOne(jsonData.menu)
            ])
            .then(() => {
                resolve();
            })
            .catch((err) => {
                console.log('Error inserting test data', err);
                reject(err);
            });
        })
        .catch((err) => {
            console.log('Error removing existing test data', err);
            reject(err);
        });
    });
};

// Adds current date to product added date when smashing into DB
function fixProductDates(products){
    let index = 0;
    products.forEach((product) => {
        products[index].productAddedDate = new Date();
        index++;
    });
    return products;
}
```

## `routes/admin.js` (changed lines (10, 422, 423, 424, 425, 426, 427, 429, 430, 432, 433, 434, 435, 436, 437, 438, 439, 440, 441, 442, 447, 448, 449, 467))

```javascript
const express = require('express');
const common = require('../lib/common');
const escape = require('html-entities').AllHtmlEntities;
const colors = require('colors');
const bcrypt = require('bcryptjs');
const fs = require('fs');
const path = require('path');
const multer = require('multer');
const glob = require('glob');
const router = express.Router();

// Admin section
router.get('/admin', common.restrict, (req, res, next) => {
    res.redirect('/admin/orders');
});

// logout
router.get('/admin/logout', (req, res) => {
    req.session.user = null;
    req.session.message = null;
    req.session.messageType = null;
    res.redirect('/');
});

// login form
router.get('/admin/login', (req, res) => {
    let db = req.app.db;

    db.users.count({}, (err, userCount) => {
        if(err){
            // if there are no users set the "needsSetup" session
            req.session.needsSetup = true;
            res.redirect('/admin/setup');
        }
        // we check for a user. If one exists, redirect to login form otherwise setup
        if(userCount > 0){
            // set needsSetup to false as a user exists
            req.session.needsSetup = false;
            res.render('login', {
                title: 'Login',
                referringUrl: req.header('Referer'),
                config: req.app.config,
                message: common.clearSessionValue(req.session, 'message'),
                messageType: common.clearSessionValue(req.session, 'messageType'),
                helpers: req.handlebars.helpers,
                showFooter: 'showFooter'
            });
        }else{
            // if there are no users set the "needsSetup" session
            req.session.needsSetup = true;
            res.redirect('/admin/setup');
        }
    });
});

// login the user and check the password
router.post('/admin/login_action', (req, res) => {
    let db = req.app.db;

    db.users.findOne({userEmail: req.body.email}, (err, user) => {
        if(err){
            res.status(400).json({message: 'A user with that email does not exist.'});
            return;
        }

        // check if user exists with that email
        if(user === undefined || user === null){
            res.status(400).json({message: 'A user with that email does not exist.'});
        }else{
            // we have a user under that email so we compare the password
            bcrypt.compare(req.body.password, user.userPassword)
            .then((result) => {
                if(result){
                    req.session.user = req.body.email;
                    req.session.usersName = user.usersName;
                    req.session.userId = user._id.toString();
                    req.session.isAdmin = user.isAdmin;
                    res.status(200).json({message: 'Login successful'});
                }else{
                    // password is not correct
                    res.status(400).json({message: 'Access denied. Check password and try again.'});
                }
            });
        }
    });
});

// setup form is shown when there are no users setup in the DB
router.get('/admin/setup', (req, res) => {
    let db = req.app.db;

    db.users.count({}, (err, userCount) => {
        if(err){
            console.error(colors.red('Error getting users for setup', err));
        }
        // dont allow the user to "re-setup" if a user exists.
        // set needsSetup to false as a user exists
        req.session.needsSetup = false;
        if(userCount === 0){
            req.session.needsSetup = true;
            res.render('setup', {
                title: 'Setup',
                config: req.app.config,
                helpers: req.handlebars.helpers,
                message: common.clearSessionValue(req.session, 'message'),
                messageType: common.clearSessionValue(req.session, 'messageType'),
                showFooter: 'showFooter'
            });
        }else{
            res.redirect('/admin/login');
        }
    });
});

// insert a user
router.post('/admin/setup_action', (req, res) => {
    const db = req.app.db;

    let doc = {
        usersName: req.body.usersName,
        userEmail: req.body.userEmail,
        userPassword: bcrypt.hashSync(req.body.userPassword, 10),
        isAdmin: true
    };

    // check for users
    db.users.count({}, (err, userCount) => {
        if(err){
            console.info(err.stack);
        }
        if(userCount === 0){
            // email is ok to be used.
            db.users.insert(doc, (err, doc) => {
                // show the view
                if(err){
                    console.error(colors.red('Failed to insert user: ' + err));
                    req.session.message = 'Setup failed';
                    req.session.messageType = 'danger';
                    res.redirect('/admin/setup');
                }else{
                    req.session.message = 'User account inserted';
                    req.session.messageType = 'success';
                    res.redirect('/admin/login');
                }
            });
        }else{
            res.redirect('/admin/login');
        }
    });
});

// settings update
router.get('/admin/settings', common.restrict, (req, res) => {
    res.render('settings', {
        title: 'Cart settings',
        session: req.session,
        admin: true,
        themes: common.getThemes(),
        message: common.clearSessionValue(req.session, 'message'),
        messageType: common.clearSessionValue(req.session, 'messageType'),
        helpers: req.handlebars.helpers,
        config: req.app.config,
        footerHtml: typeof req.app.config.footerHtml !== 'undefined' ? escape.decode(req.app.config.footerHtml) : null,
        googleAnalytics: typeof req.app.config.googleAnalytics !== 'undefined' ? escape.decode(req.app.config.googleAnalytics) : null
    });
});

// settings update
router.post('/admin/settings/update', common.restrict, common.checkAccess, (req, res) => {
    let result = common.updateConfig(req.body);
    if(result === true){
        res.status(200).json({message: 'Settings successfully updated'});
        return;
    }
    res.status(400).json({message: 'Permission denied'});
});

// settings update
router.post('/admin/settings/option/remove', common.restrict, common.checkAccess, (req, res) => {
    const db = req.app.db;
    db.products.findOne({_id: common.getId(req.body.productId)}, (err, product) => {
        if(err){
            console.info(err.stack);
        }
        if(product && product.productOptions){
            let optJson = JSON.parse(product.productOptions);
            delete optJson[req.body.optName];

            db.products.update({_id: common.getImages(req.body.productId)}, {$set: {productOptions: JSON.stringify(optJson)}}, (err, numReplaced) => {
                if(err){
                    console.info(err.stack);
                }
                if(numReplaced === 1){
                    res.status(200).json({message: 'Option successfully removed'});
                }else{
                    res.status(400).json({message: 'Failed to remove option. Please try again.'});
                }
            });
        }else{
            res.status(400).json({message: 'Product not found. Try saving before removing.'});
        }
    });
});

// settings update
router.get('/admin/settings/menu', common.restrict, async (req, res) => {
    const db = req.app.db;
    res.render('settings_menu', {
        title: 'Cart menu',
        session: req.session,
        admin: true,
        message: common.clearSessionValue(req.session, 'message'),
        messageType: common.clearSessionValue(req.session, 'messageType'),
        helpers: req.handlebars.helpers,
        config: req.app.config,
        menu: common.sortMenu(await common.getMenu(db))
    });
});

// settings page list
router.get('/admin/settings/pages', common.restrict, (req, res) => {
    const db = req.app.db;
    db.pages.find({}).toArray(async (err, pages) => {
        if(err){
            console.info(err.stack);
        }

        res.render('settings_pages', {
            title: 'Static pages',
            pages: pages,
            session: req.session,
            admin: true,
            message: common.clearSessionValue(req.session, 'message'),
            messageType: common.clearSessionValue(req.session, 'messageType'),
            helpers: req.handlebars.helpers,
            config: req.app.config,
            menu: common.sortMenu(await common.getMenu(db))
        });
    });
});

// settings pages new
router.get('/admin/settings/pages/new', common.restrict, common.checkAccess, async (req, res) => {
    const db = req.app.db;

    res.render('settings_page_edit', {
        title: 'Static pages',
        session: req.session,
        admin: true,
        button_text: 'Create',
        message: common.clearSessionValue(req.session, 'message'),
        messageType: common.clearSessionValue(req.session, 'messageType'),
        helpers: req.handlebars.helpers,
        config: req.app.config,
        menu: common.sortMenu(await common.getMenu(db))
    });
});

// settings pages editor
router.get('/admin/settings/pages/edit/:page', common.restrict, common.checkAccess, (req, res) => {
    const db = req.app.db;
    db.pages.findOne({_id: common.getId(req.params.page)}, async (err, page) => {
        if(err){
            console.info(err.stack);
        }
        // page found
        const menu = common.sortMenu(await common.getMenu(db));
        if(page){
            res.render('settings_page_edit', {
                title: 'Static pages',
                page: page,
                button_text: 'Update',
                session: req.session,
                admin: true,
                message: common.clearSessionValue(req.session, 'message'),
                messageType: common.clearSessionValue(req.session, 'messageType'),
                helpers: req.handlebars.helpers,
                config: req.app.config,
                menu
            });
        }else{
            // 404 it!
            res.status(404).render('error', {
                title: '404 Error - Page not found',
                config: req.app.config,
                message: '404 Error - Page not found',
                helpers: req.handlebars.helpers,
                showFooter: 'showFooter',
                menu
            });
        }
    });
});

// settings update page
router.post('/admin/settings/pages/update', common.restrict, common.checkAccess, (req, res) => {
    const db = req.app.db;

    let doc = {
        pageName: req.body.pageName,
        pageSlug: req.body.pageSlug,
        pageEnabled: req.body.pageEnabled,
        pageContent: req.body.pageContent
    };

    if(req.body.page_id){
        // existing page
        db.pages.findOne({_id: common.getId(req.body.page_id)}, (err, page) => {
            if(err){
                console.info(err.stack);
            }
            if(page){
                db.pages.update({_id: common.getId(req.body.page_id)}, {$set: doc}, {}, (err, numReplaced) => {
                    if(err){
                        console.info(err.stack);
                    }
                    res.status(200).json({message: 'Page updated successfully', page_id: req.body.page_id});
                });
            }else{
                res.status(400).json({message: 'Page not found'});
            }
        });
    }else{
        // insert page
        db.pages.insert(doc, (err, newDoc) => {
            if(err){
                res.status(400).json({message: 'Error creating page. Please try again.'});
            }else{
                res.status(200).json({message: 'New page successfully created', page_id: newDoc._id});
            }
        });
    }
});

// settings delete page
router.get('/admin/settings/pages/delete/:page', common.restrict, common.checkAccess, (req, res) => {
    const db = req.app.db;
    db.pages.remove({_id: common.getId(req.params.page)}, {}, (err, numRemoved) => {
        if(err){
            req.session.message = 'Error deleting page. Please try again.';
            req.session.messageType = 'danger';
            res.redirect('/admin/settings/pages');
            return;
        }
        req.session.message = 'Page successfully deleted';
        req.session.messageType = 'success';
        res.redirect('/admin/settings/pages');
    });
});

// new menu item
router.post('/admin/settings/menu/new', common.restrict, common.checkAccess, (req, res) => {
    let result = common.newMenu(req, res);
    if(result === false){
        req.session.message = 'Failed creating menu.';
        req.session.messageType = 'danger';
    }
    res.redirect('/admin/settings/menu');
});

// update existing menu item
router.post('/admin/settings/menu/update', common.restrict, common.checkAccess, (req, res) => {
    let result = common.updateMenu(req, res);
    if(result === false){
        req.session.message = 'Failed updating menu.';
        req.session.messageType = 'danger';
    }
    res.redirect('/admin/settings/menu');
});

// delete menu item
router.get('/admin/settings/menu/delete/:menuid', common.restrict, common.checkAccess, (req, res) => {
    let result = common.deleteMenu(req, res, req.params.menuid);
    if(result === false){
        req.session.message = 'Failed deleting menu.';
        req.session.messageType = 'danger';
    }
    res.redirect('/admin/settings/menu');
});

// We call this via a Ajax call to save the order from the sortable list
router.post('/admin/settings/menu/save_order', common.restrict, common.checkAccess, (req, res) => {
    let result = common.orderMenu(req, res);
    if(result === false){
        res.status(400).json({message: 'Failed saving menu order'});
        return;
    }
    res.status(200);
});

// validate the permalink
router.post('/admin/api/validate_permalink', (req, res) => {
    // if doc id is provided it checks for permalink in any products other that one provided,
    // else it just checks for any products with that permalink
    const db = req.app.db;

    let query = {};
    if(typeof req.body.docId === 'undefined' || req.body.docId === ''){
        query = {productPermalink: req.body.permalink};
    }else{
        query = {productPermalink: req.body.permalink, _id: {$ne: common.getId(req.body.docId)}};
    }

    db.products.count(query, (err, products) => {
        if(err){
            console.info(err.stack);
        }
        if(products > 0){
            res.status(400).json({message: 'Permalink already exists'});
        }else{
            res.status(200).json({message: 'Permalink validated successfully'});
        }
    });
});

// upload the file
let upload = multer({dest: 'public/uploads/'});
router.post('/admin/file/upload', common.restrict, common.checkAccess, upload.single('upload_file'), (req, res, next) => {
    const db = req.app.db;

    if(req.file){
        // check for upload select
        let uploadDir = path.join('public/uploads', req.body.directory);

        // Check directory and create (if needed)
        common.checkDirectorySync(uploadDir);

        let file = req.file;
        let source = fs.createReadStream(file.path);
        let dest = fs.createWriteStream(path.join(uploadDir, file.originalname.replace(/ /g, '_')));

        // save the new file
        source.pipe(dest);
        source.on('end', () => { });

        // delete the temp file.
        fs.unlink(file.path, (err) => {
            if(err){
                console.info(err.stack);
            }
        });

        // get the product form the DB
        db.products.findOne({_id: common.getId(req.body.productId)}, (err, product) => {
            if(err){
                console.info(err.stack);
            }
            let imagePath = path.join('/uploads', req.body.directory, file.originalname.replace(/ /g, '_'));

            // if there isn't a product featured image, set this one
            if(!product.productImage){
                db.products.update({_id: common.getId(req.body.productId)}, {$set: {productImage: imagePath}}, {multi: false}, (err, numReplaced) => {
                    if(err){
                        console.info(err.stack);
                    }
                    req.session.message = 'File uploaded successfully';
                    req.session.messageType = 'success';
                    res.redirect('/admin/product/edit/' + req.body.productId);
                });
            }else{
                req.session.message = 'File uploaded successfully';
                req.session.messageType = 'success';
                res.redirect('/admin/product/edit/' + req.body.productId);
            }
        });
    }else{
        req.session.message = 'File upload error. Please select a file.';
        req.session.messageType = 'danger';
        res.redirect('/admin/product/edit/' + req.body.productId);
    }
});

// delete a file via ajax request
router.post('/admin/testEmail', common.restrict, (req, res) => {
    let config = req.app.config;
    // TODO: Should fix this to properly handle result
    common.sendEmail(config.emailAddress, 'expressCart test email', 'Your email settings are working');
    res.status(200).json({message: 'Test email sent'});
});

// delete a file via ajax request
router.post('/admin/file/delete', common.restrict, common.checkAccess, (req, res) => {
    req.session.message = null;
    req.session.messageType = null;

    fs.unlink('public/' + req.body.img, (err) => {
        if(err){
            console.error(colors.red('File delete error: ' + err));
            res.writeHead(400, {'Content-Type': 'application/text'});
            res.end('Failed to delete file: ' + err);
        }else{
            res.writeHead(200, {'Content-Type': 'application/text'});
            res.end('File deleted successfully');
        }
    });
});

router.get('/admin/files', common.restrict, (req, res) => {
    // loop files in /public/uploads/
    glob('public/uploads/**', {nosort: true}, (er, files) => {
        // sort array
        files.sort();

        // declare the array of objects
        let fileList = [];
        let dirList = [];

        // loop these files
        for(let i = 0; i < files.length; i++){
            // only want files
            if(fs.lstatSync(files[i]).isDirectory() === false){
                // declare the file object and set its values
                let file = {
                    id: i,
                    path: files[i].substring(6)
                };

                // push the file object into the array
                fileList.push(file);
            }else{
                let dir = {
                    id: i,
                    path: files[i].substring(6)
                };

                // push the dir object into the array
                dirList.push(dir);
            }
        }

        // render the files route
        res.render('files', {
            title: 'Files',
            files: fileList,
            admin: true,
            dirs: dirList,
            session: req.session,
            config: common.get(),
            message: common.clearSessionValue(req.session, 'message'),
            messageType: common.clearSessionValue(req.session, 'messageType')
        });
    });
});

module.exports = router;
```
