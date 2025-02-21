import os

import pandas as pd

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
RAW_DATA_DIR = os.path.join(BASE_DIR, 'data', 'raw')
PROCESSED_DATA_DIR = os.path.join(BASE_DIR, 'data', 'processed')

def load_retail_data(filename):
    filepath = os.path.join(RAW_DATA_DIR, filename)
    try:
        retail_df = pd.read_csv(filepath)

        # Info on dataset
        print ("Retail Sales Data:")
        print (retail_df.info())
        print (retail_df.head())
        return retail_df
    except Exception as e:
        print (f"Error loading {filepath}: {e}")
        return None

def load_churn_data(filename):
    filepath = os.path.join(RAW_DATA_DIR, filename)
    try:
        churn_df = pd.read_csv(filepath)

        if 'Years_as_Customer' in churn_df.columns:
            churn_df['Years_as_Customer'] = pd.to_numeric(churn_df['Years_as_Customer'])

        print ("Customer Churn Data:")
        print (churn_df.info())
        print (churn_df.head())
        return churn_df
    except Exception as e:
        print (f"Error loading {filepath}: {e}")
        return None

def clean_data(df):
    df = df.drop_duplicates()
    df = df.ffill()

    if 'Customer ID' in df.columns:
        df = df.rename(columns={'Customer ID': 'id'})
    if 'Customer_ID' in df.columns:
        df = df.rename(columns={'Customer_ID': 'id'})

    df['id'] = df['id'].astype(str)
    df['id'] = df['id'].str.strip().str.lower()

    return df

def merge_id_outer(df_left, df_right):
    assert df_left['id'].is_unique, "Duplicate IDs in left df!"
    assert df_right['id'].is_unique, "Duplicate IDs in right df!"
    merged_df = pd.merge(df_left, df_right, on='id', how='outer', indicator=True)
    merged_df = merged_df.ffill()
    print (merged_df['_merge'].value_counts())
    print(merged_df.describe())
    return merged_df

def churn (df):
    category_stats = df.groupby('Product Category').agg({
        'Price per Unit': 'mean',
        'Quantity': 'sum',
        'Num_of_Returns': 'sum',
        'Target_Churn': 'mean'
    }).reset_index()

    print (category_stats)
    category_stats.to_csv(os.path.join(PROCESSED_DATA_DIR, 'category_stats.csv'))
    return category_stats

def customer_info (df):
    customer_stats = df.groupby('id').agg({
        'Gender': 'first',
        'Age': 'mean',
        'Annual_Income': 'mean',
        'Total_Spend': 'sum',
        'Years_as_Customer': 'max',
        'Num_of_Purchases': 'sum',
        'Average_Transaction_Amount': 'mean',
        'Num_of_Returns': 'sum',
        'Satisfaction': 'mean',
        'Last_Purchase_Days_Ago': 'min',
        'Target_Churn': 'mean',
        'Email_Opt_In': 'mean',
        'Promotion_Response': 'first'
    }).reset_index()

    print(customer_stats)
    customer_stats.to_csv(os.path.join(PROCESSED_DATA_DIR, 'customer_stats.csv'))
    return customer_stats

def transaction_info (df):
    transaction_stats = df.groupby('id').agg({
        'Transaction ID': 'first',
        'id': 'first',
        'Product Category': 'first',
        'Quantity': 'sum',
        'Price per Unit': 'sum',
        'Total Amount': 'sum',
        'Satisfaction': 'mean',
    })

    print(transaction_stats)
    transaction_stats.to_csv(os.path.join(PROCESSED_DATA_DIR, 'transaction_info.csv'))
    return transaction_stats

def process_data (retail_path, churn_path, output_dir):
    # Load data
    retail_df = load_retail_data(retail_path)
    churn_df = load_churn_data(churn_path)

    # Clean data
    retail_df = clean_data(retail_df)
    churn_df = clean_data(churn_df)

    # Link data frames
    merged_df = merge_id_outer(retail_df, churn_df)

    # Create output dir if it doesn't exist
    os.makedirs(output_dir, exist_ok=True)

    # Features
    customers = customer_info(merged_df)
    churn_per_category = churn(merged_df)
    transactions = transaction_info(merged_df)

    # Save processed data to CSV files
    retail_output_path = os.path.join(output_dir, 'retail_sales_processed.csv')
    churn_output_path = os.path.join(output_dir, 'customer_churn_processed.csv')
    merged_output_path = os.path.join(output_dir, 'merged_sales.csv')
    customer_info_output_path = os.path.join(output_dir, 'customer_info.csv')
    churn_info_output_path = os.path.join(output_dir, 'churn_info.csv')
    transaction_info_output_path = os.path.join(output_dir, 'transaction_info.csv')

    merged_df.to_csv(merged_output_path, index=False)
    retail_df.to_csv(retail_output_path, index=False)
    churn_df.to_csv(churn_output_path, index=False)
    customers.to_csv(customer_info_output_path, index=False)
    churn_per_category.to_csv(customer_info_output_path, index=False)
    transactions.to_csv(transaction_info_output_path, index=False)

    print ("Processed data saved to:")
    print (" -", retail_output_path)
    print (" -", churn_output_path)
    print (" -", merged_output_path)
    print (" -", customer_info_output_path)
    print (" -", churn_info_output_path)
    print (" -", transaction_info_output_path)

    return merged_df

if __name__ == "__main__":
    # Define file paths relative to the project root
    retail_file = os.path.join('retail_sales_dataset.csv')
    churn_file = os.path.join('online_retail_customer_churn.csv')
    processed_dir = PROCESSED_DATA_DIR

    # Process and save data
    data = process_data(retail_file, churn_file, processed_dir)